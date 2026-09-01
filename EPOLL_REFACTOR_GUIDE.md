# NetDisk 服务端 epoll 改造手册

## 1. 目标与边界

当前服务端由主线程阻塞在 `accept()`，连接建立后立即提交给固定线程池。工作线程调用阻塞式 `net_read_full()`/`net_write_full()` 处理一条请求并关闭连接。

`epoll` 是 Linux 专用接口，因此启用本方案后 C 服务端的构建与运行目标平台是 Linux；如需继续支持 macOS，应在 Reactor 接口下另行实现 `kqueue` 后端。

本次改造采用两阶段路线：

1. **阶段一（本次实现）——epoll 接入层 + 有界工作池**
   - 主线程运行 epoll Reactor。
   - 监听 socket 和尚未发送请求的客户端 socket 均为非阻塞。
   - 新连接先进入 epoll，不占用工作线程。
   - 客户端 socket 首次可读后，从 epoll 删除、恢复阻塞模式，再把所有权移交给工作池。
   - 工作池继续运行现有协议处理器，保持全部客户端和 Web 桥接协议兼容。
2. **阶段二（后续演进）——全非阻塞协议状态机**
   - Reactor 保存每个连接的命令、字段长度、字段内容、上传/下载偏移量和输出队列。
   - socket I/O 全部由 Reactor 完成；工作线程仅执行 SQLite、目录遍历等阻塞业务。
   - 工作完成后使用 `eventfd` 唤醒 Reactor。

阶段一已经真正使用 epoll 管理连接就绪事件，主要解决“空连接立刻占用 worker”的问题。它不会消除已经开始发送请求的慢客户端对 worker 的占用；该问题属于阶段二。

## 2. 阶段一架构

```text
                        +-------------------+
listen socket -------->|                   |
pending client socket ->|  epoll Reactor    |
                        |  (主线程)          |
                        +---------+---------+
                                  |
                           首次 EPOLLIN
                                  |
                    EPOLL_CTL_DEL + 清除 O_NONBLOCK
                                  |
                                  v
                        +-------------------+
                        | bounded thread    |
                        | pool              |
                        +---------+---------+
                                  |
                                  v
                        handle_client(fd)
                                  |
                           worker 关闭 fd
```

### 2.1 socket 所有权

必须遵守单一所有权：

- pending 状态：Reactor 拥有 fd，负责 `epoll_ctl()` 和关闭。
- `thread_pool_submit()` 成功后：工作池拥有 fd，Reactor 不再访问或关闭。
- 提交失败：Reactor 关闭 fd。
- handler 返回后：工作池关闭 fd。

### 2.2 事件模式

阶段一采用：

```c
EPOLLIN | EPOLLRDHUP | EPOLLONESHOT
```

`EPOLLONESHOT` 防止同一个 pending 连接被重复分派。连接从 epoll 删除后才移交工作池，因此不需要重新 arm。

监听 socket 和 `accept4()` 返回的新 socket 都必须包含 `SOCK_NONBLOCK`。监听事件到达后必须循环 `accept4()`，直到返回 `EAGAIN/EWOULDBLOCK`。

### 2.3 超时

- pending 连接使用 `CLOCK_MONOTONIC` 记录接入时间。
- `epoll_wait()` 每秒返回一次，扫描并关闭超过 `--client-timeout` 的 pending 连接。
- 移交 worker 前清除 `O_NONBLOCK`，并设置 `SO_RCVTIMEO/SO_SNDTIMEO`，继续保护阻塞式 handler。

## 3. 文件布局

```text
server/include/epoll_server.h
server/src/epoll_server.c
```

`epoll_server_run()` 负责：

- 创建和关闭 epoll fd；
- 注册监听 socket；
- 接收 pending 连接；
- 管理 pending 链表和超时；
- 把首次可读连接安全移交工作池；
- 统计队列满导致的拒绝连接数。

## 4. 主程序改造

原来的循环：

```c
accept_client();
configure_client_socket();
thread_pool_submit();
```

替换为：

```c
struct epoll_server_stats stats = {0};

result = epoll_server_run(listen_fd, pool,
                          options.client_timeout_seconds,
                          &stop_requested, &stats);
```

信号线程继续设置 `stop_requested` 并 `shutdown(listen_fd)`；Reactor 最迟在下一次 epoll 超时后退出。

## 5. 阶段二设计

阶段二不能继续在 Reactor 中调用 `handle_client()`，需要为每个连接维护状态：

```c
enum connection_state {
    CONN_RECV_COMMAND,
    CONN_RECV_FIELD_LENGTH,
    CONN_RECV_FIELD_DATA,
    CONN_RECV_UPLOAD_SIZE,
    CONN_RECV_UPLOAD_BODY,
    CONN_EXECUTE,
    CONN_SEND_BUFFER,
    CONN_SEND_FILE,
    CONN_FINISHED,
    CONN_ERROR
};
```

每次 `recv()`/`send()`：

- 成功：累计 offset；
- `EINTR`：重试；
- `EAGAIN/EWOULDBLOCK`：保存状态并返回事件循环；
- `0`：对端关闭；
- 其他错误：关闭连接。

上传正文按固定预算读取并写临时文件；下载按 `EPOLLOUT` 分块读取文件并发送。任何一次事件处理都不能无限循环，以免一个大文件饿死其他连接。

## 6. 验收标准

### 阶段一

- 进程能够观察到 `epoll_create1/epoll_ctl/epoll_wait`。
- 大量只建立连接、不发送数据的客户端不占用 worker。
- 首次可读连接只被提交一次。
- 队列满时连接被明确拒绝，服务端不崩溃。
- pending 连接和 worker 中连接均受超时限制。
- 原有注册、登录、上传、下载、目录和线程池测试通过。
- SIGINT/SIGTERM 可以唤醒并正常关闭服务端。

### 阶段二

- Reactor 不调用 `net_read_full()`/`net_write_full()`。
- 慢上传和慢下载不会独占 worker。
- `EPOLLOUT` 只在有待发送数据时开启。
- socket 只由 Reactor 关闭。
- 分片字段、截断请求、半关闭和大文件均有集成测试。

## 7. 调试命令

```bash
make clean
make
make test-server
```

确认 epoll 系统调用：

```bash
strace -f \
  -e trace=epoll_create1,epoll_ctl,epoll_wait,accept4 \
  ./server/server
```

检查线程和 fd：

```bash
ls /proc/<pid>/task
ls /proc/<pid>/fd
```
