# NetDisk 技术说明书

本文档面向项目维护者、答辩说明、二次开发者和代码评审人员，重点说明本项目中关键、复杂、具有工程含量的技术是如何落地实现的。内容基于当前仓库源码整理，覆盖 C 核心服务、命令行客户端、Flask Web 桥接层、浏览器前端、eBPF 调试器、构建启动脚本以及当前实现中的技术风险。

## 1. 项目定位与总体架构

NetDisk 是一个轻量级网盘系统，核心文件服务由 C 语言实现，Web 层由 Flask 负责 HTTP API 和静态页面，浏览器前端负责交互式文件管理，命令行客户端提供类 Shell 的使用体验，eBPF 调试器用于观测网络、系统调用和性能事件。

整体架构如下：

```text
Browser
  |
  | HTTP / JSON / multipart
  v
Flask Web Bridge (:8080)
  |
  | 自定义 TCP 二进制协议
  v
C Server (:9000)
  |
  | SQLite + POSIX 文件系统
  v
netdisk.db + netdisk_data/<username>/

CLI Client
  |
  | 自定义 TCP 二进制协议
  v
C Server (:9000)

eBPF Debugger (:8090)
  |
  | kprobe / tracepoint / perf event / uprobe
  v
Linux Kernel + C Server / Client Process
```

核心设计思想：

- C Server 是唯一真正操作用户数据和认证数据库的核心服务。
- Web Server 不直接改写文件存储，而是作为协议桥，把 HTTP 请求翻译成 C Server 可以理解的 TCP 命令。
- CLI Client 直接使用同一套 TCP 协议访问 C Server。
- 文件隔离不依赖数据库记录每个文件，而是使用 `netdisk_data/<username>/` 作为每个用户的物理根目录。
- eBPF Debugger 不侵入业务代码，通过内核探针和用户态函数探针采集运行时行为。

主要组件：

| 组件 | 入口文件 | 技术栈 | 作用 |
|---|---|---|---|
| C Server | `server/src/main.c` | C、POSIX Socket、pthread、SQLite、OpenSSL SHA-256 | 用户认证、文件上传下载、目录操作、目录树、协议处理 |
| CLI Client | `client/src/main.c` | C、Readline、POSIX Socket | 命令行交互、自动补全、上传下载进度显示 |
| Web Bridge | `gui_examples/app.py` | Flask、Flask-CORS、Python socket | REST API、静态资源服务、HTTP 到 TCP 协议转换 |
| Web Frontend | `gui_examples/js/*.js` | HTML、CSS、ES6、Fetch API | 登录状态、文件列表、上传队列、批量下载、国际化 |
| eBPF Debugger | `ebpf_debugger/app.py` | Flask-SocketIO、BCC、psutil | 网络、系统调用、性能、用户态函数追踪 |
| 启停脚本 | `start.sh`、`stop.sh` | Bash | 编译、依赖检查、服务启动和清理 |

## 2. C Server 核心服务实现

C Server 是项目的核心数据面，监听 `0.0.0.0:9000`，每个 TCP 连接只处理一条命令。服务端先读取 1 字节命令码，再按该命令约定的字段顺序读取后续数据。

### 2.1 服务启动与监听模型

`server/src/main.c` 的 `main()` 完成以下动作：

1. 打开或创建 SQLite 数据库 `netdisk.db`。
2. 执行建表语句，确保用户表存在。
3. 创建 TCP socket。
4. 绑定端口 `9000`。
5. 调用 `listen()` 进入监听状态。
6. 在无限循环中 `accept()` 新连接。
7. 每个连接分配一个独立 `pthread` 执行 `handle_client()`。
8. 线程被 `pthread_detach()`，避免主线程回收 join。

关键点：

- 主线程只负责接收连接，不做业务处理。
- 每个连接在独立线程中完成，避免单个上传、下载或目录遍历阻塞其他请求。
- 每个线程内部独立打开 SQLite 连接，降低跨线程共享数据库句柄的复杂度。

线程模型：

```text
main thread
  |
  +-- accept conn A -> pthread(handle_client)
  +-- accept conn B -> pthread(handle_client)
  +-- accept conn C -> pthread(handle_client)
```

这种模型实现简单，适合轻量级局域网网盘。它的代价是线程数量会随并发连接线性增长，面对大量连接时需要连接池、事件驱动或线程池优化。

### 2.2 用户认证与 SHA-256 密码摘要

用户数据保存在 SQLite：

```sql
CREATE TABLE IF NOT EXISTS users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT UNIQUE,
    password TEXT
);
```

注册命令 `R` 的处理流程：

1. 读取用户名长度 `ulen`，网络字节序 4 字节整数。
2. 读取用户名字符串。
3. 读取密码长度 `plen`。
4. 读取明文密码字符串。
5. 使用 `sha256_string()` 计算 64 字节十六进制 SHA-256 摘要。
6. 通过 SQLite 预编译语句插入 `users` 表。
7. 注册成功后创建 `netdisk_data/<username>/` 用户目录。
8. 返回 1 字节结果，`1` 表示成功，`0` 表示失败。

登录命令 `L` 的处理流程：

1. 读取用户名和密码。
2. 查询数据库中该用户的密码摘要。
3. 对客户端传来的密码计算 SHA-256。
4. 比较摘要是否一致。
5. 返回 1 字节结果。

实现价值：

- 使用 OpenSSL 的 `SHA256()`，避免手写摘要算法。
- 使用 SQLite prepared statement 和 `sqlite3_bind_text()`，避免直接拼接 SQL。
- 服务端不保存明文密码，只保存摘要。

当前限制：

- SHA-256 未加盐，不适合作为生产级密码存储方案。
- 登录和注册传输过程未加 TLS，局域网外使用存在明文密码被截获风险。
- 服务器日志中存在调试输出用户名和密码的代码，生产环境应移除。

### 2.3 用户目录隔离与路径构造

项目使用物理目录隔离用户文件：

```text
netdisk_data/
  alice/
    docs/
    photo.jpg
  bob/
    report.pdf
```

相关函数：

- `ensure_user_dir(username)`：确保 `netdisk_data/` 和 `netdisk_data/<username>/` 存在。
- `mkdirs(path)`：递归创建多级目录。

服务端构造路径时基本模式为：

```text
netdisk_data/<username>/<relative_path>
```

如果传入目录以 `/` 开头，则拼接为：

```text
netdisk_data/<username><absolute_like_path>
```

如果传入目录不以 `/` 开头，则拼接为：

```text
netdisk_data/<username>/<relative_path>
```

实现价值：

- 多用户之间天然隔离。
- 不需要数据库维护复杂的文件元数据树。
- 目录层级由操作系统文件系统直接承载。

当前限制：

- 当前服务端主要依赖字符串拼接构造路径，未统一做路径规范化和根目录逃逸检查。
- 如果上层传入 `../` 形式路径，存在目录穿越风险。
- 删除目录时当前实现会在 `rmdir()` 失败后拼接 `rm -rf "<path>"` 并调用 `system()`，这是高风险实现，应改为安全的递归删除函数。

### 2.4 自定义 TCP 二进制协议

项目自定义了一个非常轻量的 TCP 协议。所有命令以 1 字节命令码开始，字符串使用 “4 字节网络序长度 + UTF-8 字节串” 表示，文件大小使用高 32 位和低 32 位拆分传输。

通用字符串格式：

```text
uint32 length_be
byte[length] content
```

64 位文件大小格式：

```text
uint32 size_high_be
uint32 size_low_be
file_size = (size_high << 32) | size_low
```

主要命令：

| 命令码 | 名称 | 请求字段 | 响应 |
|---|---|---|---|
| `R` | 注册 | username, password | 1 字节成功标志 |
| `L` | 登录 | username, password | 1 字节成功标志 |
| `U` | 上传 | username, target_dir, filename, file_size, file bytes | 1 字节成功标志 |
| `D` | 下载 | username, current_dir, filename | exists flag, file_size, file bytes |
| `S` | 目录列表 | username, target_dir | success flag, count, entries |
| `X` | 删除 | username, current_dir, filename | 1 字节成功标志 |
| `M` | 创建目录 | username, dirpath | 1 字节成功标志 |
| `T` | 创建空文件 | username, filepath | 1 字节成功标志 |
| `V` | 验证目录 | username, path | 1 字节是否存在 |
| `Y` | 目录树 | username, dir_path | success flag, tree entries, end marker |
| `N` | 重命名 | username, old_path, new_path | 1 字节成功标志 |

目录列表 `S` 的响应结构：

```text
char success
uint32 count_be
repeat count times:
  uint32 type_be       # 1=file, 2=directory
  uint32 name_len_be
  byte[name_len] name
  uint64 size_be
  uint64 mtime_be
```

目录树 `Y` 的响应结构：

```text
char success
repeat:
  char type            # 1=file, 2=directory, 0=end
  uint32 name_len_be
  byte[name_len] name
  uint32 depth_be
```

协议实现的含金量点：

- 跨语言互通：C Client、Python Web Bridge 都能通过同一套协议访问 C Server。
- 显式字节序：整数使用 `htonl()`、`ntohl()`、`htobe64()`、`be64toh()`，避免不同平台大小端差异。
- 大文件支持：文件大小使用 64 位逻辑拆成两个 32 位网络序字段，突破 4GB 限制。
- 半结构化目录数据：目录列表不仅返回名称，还返回类型、大小和修改时间，支持前端表格渲染。

当前协议实现的注意点：

- 代码大量直接调用 `read()` 和 `write()`，没有封装 `read_full()` / `write_full()`。TCP 是字节流，单次 `read()` 不保证读满指定长度，大文件或复杂网络环境下可能出现半包问题。
- 部分客户端和 Flask 路由对同一命令的字段顺序存在不一致，详见本文 “当前技术风险”。
- 协议没有版本号、魔数、请求 ID 或错误码体系，后续扩展时兼容性较弱。

### 2.5 文件上传实现

服务端 `U` 命令处理逻辑：

1. 读取用户名。
2. 读取目标目录。
3. 读取文件名。
4. 调用 `ensure_user_dir()` 确保用户根目录存在。
5. 若指定目标目录，调用 `mkdirs()` 递归创建。
6. 拼接最终文件路径。
7. `fopen(filepath, "wb")` 创建目标文件。
8. 读取 64 位文件大小。
9. 循环从 socket 读取文件内容，每次最多 4096 字节。
10. 写入本地文件。
11. 当累计接收字节等于声明的文件大小时返回成功。

核心循环思想：

```text
while total_received < file_size:
    want = min(4096, file_size - total_received)
    n = read(conn_fd, buffer, want)
    fwrite(buffer, n, fp)
    total_received += n
```

实现价值：

- 基于流式读取，不需要把整个文件一次性放入内存。
- 服务端以声明的文件大小作为终止条件，不依赖连接关闭判断文件结束。
- 支持上传到任意子目录，并自动创建目标目录。

Web 上传层的额外处理：

- Flask `upload_file()` 从 `multipart/form-data` 读取文件。
- 使用上传专用 socket，并配置更长的 `UPLOAD_TIMEOUT`。
- 发送 `U` 命令后，按协议发送 username、path、filename、file_size 和 file bytes。
- 前端上传管理器对超时做“可能已上传成功，刷新列表确认”的容错处理。

当前限制：

- Flask 上传会先 `file.read()` 把整个文件读入内存，再发送给 C Server。大文件上传时会占用较多内存。
- 服务端若中途接收失败，已创建的半截文件不会自动清理。
- CLI 上传实现当前没有按协议发送 file_size，见风险章节。

### 2.6 文件下载实现

服务端 `D` 命令处理逻辑：

1. 读取用户名。
2. 读取当前目录路径。
3. 读取文件名。
4. 拼接完整路径。
5. `fopen(filepath, "rb")` 打开文件。
6. 若不存在，返回 `flag=0`。
7. 若存在，返回 `flag=1`。
8. 使用 `fseek()` 和 `ftell()` 获取文件大小。
9. 拆分高低 32 位发送大小。
10. 使用 4096 字节缓冲循环发送文件内容。

CLI 下载端：

- 发送当前目录 `g_current_dir` 和文件名。
- 读取存在标志和文件大小。
- 在当前本地目录创建同名文件。
- 循环读取 socket 数据并写入本地文件。
- 使用 `gettimeofday()` 计算平均速度，通过 `show_progress()` 输出进度条。

当前限制：

- CLI 下载端循环读取直到连接关闭，没有严格按照 `file_size` 停止。通常可以工作，但更稳妥的方式是按 `downloaded_size < file_size` 控制循环。
- Web 下载路由当前发送字段和 C Server `D` 命令期望不一致，详见风险章节。

### 2.7 目录列表、目录树和路径导航

目录列表命令 `S` 是 Web 和 CLI 展示文件管理界面的核心。

服务端实现步骤：

1. 读取用户名和目标目录。
2. 拼接到 `netdisk_data/<username>/...`。
3. `opendir()` 打开目录。
4. 第一遍遍历统计条目数量，跳过 `.` 和 `..`。
5. `rewinddir()` 回到目录开头。
6. 第二遍遍历每个条目，使用 `stat()` 判断类型、大小和修改时间。
7. 按协议返回条目。

CLI 侧：

- `send_list_files()` 解析目录条目并以表格输出。
- 使用 `format_size()` 把字节数转换为 B、KB、MB、GB、TB。
- 使用 `localtime()` 和 `strftime()` 格式化修改时间。

Web 侧：

- `FileManager.refreshList()` 调用 `/api/files?username=...&path=...`。
- 后端转成 `S` 命令。
- 返回 JSON 后由 `FileManager.loadFileList()` 渲染表格。
- 目录显示为文件夹类型，点击可进入。

目录树命令 `Y`：

- 服务端通过 `send_directory_tree()` 递归 DFS 遍历目录。
- 每个节点返回类型、名称和深度。
- 客户端根据深度打印缩进树。

路径导航：

- CLI 维护全局 `g_current_dir`。
- `normalize_path()` 处理 `.` 和 `..`。
- 切换目录前发送 `V` 命令到服务端验证路径是否真实存在。

这部分的价值在于：路径状态主要放在客户端，服务端只验证和执行文件系统操作，从而让 C Server 保持无会话、无状态。

### 2.8 文件删除、创建目录、创建空文件、重命名

删除命令 `X`：

- 读取 username、current_dir、filename。
- 拼接完整路径。
- `stat()` 判断目标存在。
- 文件使用 `remove()` 删除。
- 空目录先尝试 `rmdir()`。
- 非空目录当前使用 `system("rm -rf ...")` 作为兜底。

创建目录命令 `M`：

- 读取 username 和目录路径。
- 调用 `ensure_user_dir()`。
- 拼接用户根目录下的完整路径。
- 调用 `mkdirs()` 支持多级目录创建。

创建空文件命令 `T`：

- 拼接完整文件路径。
- `fopen(full_path, "a")` 创建或更新时间戳。

重命名命令 `N`：

- 读取 old_path 和 new_path。
- 分别拼接为用户根目录下完整路径。
- 使用 POSIX `rename()` 完成移动或改名。

设计特点：

- 文件和目录管理主要复用 POSIX 文件系统能力。
- 服务端返回统一 1 字节布尔结果，调用端处理简单。
- 重命名支持同目录改名，也可以支持路径变化形式的移动。

## 3. CLI Client 实现

CLI Client 是一套直接访问 C Server 的命令行客户端，文件位于 `client/src/main.c`。

### 3.1 交互式 Shell 与 Readline

客户端使用 GNU Readline：

- `readline(prompt)` 读取命令。
- `add_history(line)` 保存历史。
- `rl_attempted_completion_function = command_completion` 注册 Tab 补全回调。

提示符会根据登录状态和当前目录变化：

```text
Netdisk> 
Netdisk[alice]/> 
Netdisk[alice]/docs> 
```

支持命令：

```text
register, login, upload, download, list, delete, mkdir,
touch, cd, pwd, tree, help, exit
```

### 3.2 智能补全机制

补全机制分三层：

1. 命令补全：在行首补全 `register`、`login`、`upload` 等命令。
2. 服务器条目补全：登录后通过 `S` 命令拉取当前目录文件列表。
3. 按命令语义过滤：
   - `cd` 只补全目录。
   - `download` 只补全文件。
   - `delete` 同时补全文件和目录。
   - `upload` 根据参数位置区分本地文件补全和服务器目录补全。

相关结构：

```c
typedef struct {
    char name[MAX_FILENAME_LEN];
    int is_dir;
} FileEntry;
```

实现价值：

- 补全数据来自服务端真实目录，而不是本地文件系统。
- 目录补全自动追加 `/`，交互体验接近 Shell。
- `fetch_server_entries()` 通过 `S` 命令同步当前目录条目，避免客户端缓存长期陈旧。

当前限制：

- 补全缓存只在特定操作后刷新，若其他客户端修改了目录，可能短时间显示旧数据。
- 文件名包含空格时，当前 `strtok()` 参数解析无法正确处理。

### 3.3 上传下载进度条

CLI 的上传和下载都使用 `gettimeofday()` 记录开始时间，再根据已传输字节数计算平均速度。

进度条显示内容：

```text
上传 file.txt: [====================>                   ]  52.1% (1.23 MB/s)
```

核心计算：

```text
progress = transferred / total
speed = transferred / elapsed_seconds
```

速度单位会自动选择 B/s、KB/s 或 MB/s。

实现价值：

- 大文件传输时用户能看到明确反馈。
- 不依赖第三方 UI 库。
- 使用 `\r` 回到行首实现单行刷新。

## 4. Flask Web 桥接层实现

Web 桥接层位于 `gui_examples/`。它不是直接业务服务，而是 HTTP 到 C Server TCP 协议的适配器。

### 4.1 Flask 应用结构

入口 `gui_examples/app.py`：

- 创建 Flask app。
- 启用 CORS。
- 注册认证蓝图 `auth_bp`。
- 注册文件蓝图 `file_bp`。
- 提供静态页面：
  - `/` -> `login.html`
  - `/login.html`
  - `/dashboard.html`
- 提供静态资源：
  - `/js/<filename>`
  - `/css/<filename>`
- 提供健康检查 `/api/health`，同时检查 C Server 连接。

蓝图分层：

| 文件 | 作用 |
|---|---|
| `api/auth_routes.py` | 登录、注册 |
| `api/file_routes.py` | 文件列表、上传、下载、批量下载、删除、建目录、重命名 |
| `api/c_server_client.py` | C Server TCP 协议客户端封装 |
| `api/config.py` | 端口、域名、超时、上传限制等配置 |

### 4.2 CServerClient 协议封装

`CServerClient` 是 Web 层复用协议的关键：

- `connect()` 创建 TCP socket、设置 timeout、连接 `127.0.0.1:9000`。
- `send_string()` 把 Python 字符串转 UTF-8，并用 `struct.pack('!I', length)` 发送网络序长度。
- `recv_response()` 接收 1 字节结果。
- `recv_file_list()` 按 `S` 命令响应格式解析目录条目。
- `recv_file_content()` 按 `D` 命令响应格式读取文件内容。

实现价值：

- Python Web 层不需要知道 C 端内部实现，只需要遵守协议。
- 协议解析集中在一个类里，减少路由函数重复代码。
- `struct.pack('!I')` 与 C 端 `htonl()` 对齐。

### 4.3 认证 API 到 C 协议转换

登录 `/api/login`：

```text
HTTP POST /api/login
JSON { username, password }
  |
  v
TCP: 'L' + username + password
  |
  v
C Server 返回 1 字节结果
  |
  v
JSON { success, message }
```

注册 `/api/register`：

- Web 层先校验用户名长度和密码长度。
- 再发送 `R` 命令。
- C Server 执行真正的去重、哈希和落库。

这是一种双层校验模式：

- 前端和 Flask 提供用户体验层面的快速校验。
- C Server 作为最终可信边界。

### 4.4 文件列表 API

`GET /api/files?username=alice&path=/docs`

Web 桥接层执行：

1. 校验 username。
2. 创建到 C Server 的 socket。
3. 发送 `S` 命令。
4. 发送 username。
5. 把 `/docs` 转成 `docs` 发送给 C Server。
6. 读取成功标志。
7. 调用 `recv_file_list()` 解析条目。
8. 返回 JSON：

```json
{
  "success": true,
  "files": [
    {
      "name": "a.txt",
      "type": 1,
      "size": 1234,
      "mtime": 1710000000
    }
  ]
}
```

这种设计让前端不需要理解二进制协议，只消费普通 JSON。

### 4.5 Web 上传 API

`POST /api/upload` 接收 multipart：

- `file`：上传文件。
- `username`：当前用户。
- `path`：目标目录。

处理流程：

1. 使用 `create_upload_connection()` 创建上传专用连接。
2. 发送 `U` 命令。
3. 发送 username。
4. 发送 path，去掉开头 `/`。
5. 发送 filename。
6. 读取完整文件内容到内存。
7. 拆分并发送 64 位文件大小。
8. 发送文件内容。
9. 等待 1 字节上传结果。

上传专用设计：

- 使用 `UPLOAD_TIMEOUT = 120`，比普通请求更长。
- 单独封装 `send_string_upload()` 和 `recv_upload_response()`，便于调试上传链路。
- 若没有收到响应但可能已经上传，返回“上传完成，请刷新检查”类提示，前端随后刷新列表验证。

当前限制：

- `file.read()` 一次性读入内存，不适合特别大的文件。
- 更好的方式是从 Flask 文件流分块读取，并逐块 `sendall()` 给 C Server。

### 4.6 批量下载实现

Web 批量下载接口 `/api/batch-download` 的设计是服务端聚合打包：

1. 前端提交多个文件路径。
2. Flask 为每个文件单独建立到 C Server 的下载连接。
3. 每次发送 `D` 命令获取文件内容。
4. 使用 `io.BytesIO()` 创建内存缓冲区。
5. 使用 `zipfile.ZipFile(..., ZIP_DEFLATED)` 写入多个文件。
6. 返回 `application/zip` 响应。

实现价值：

- C Server 不需要理解 ZIP 格式。
- 批量下载能力完全由 Web 桥接层组合已有单文件下载协议实现。
- 浏览器只需要下载一个 ZIP。

当前限制：

- ZIP 在内存中构建，大量大文件会占用较多内存。
- 当前文件路径发送和 C Server 下载协议存在不一致，详见风险章节。

## 5. 浏览器前端实现

前端位于 `gui_examples/js/`，按职责拆分为多个管理器类。

### 5.1 API 地址自动检测

`api-config.js` 使用浏览器当前地址自动构造 API 基础地址：

```text
protocol = window.location.protocol
hostname = window.location.hostname
port = window.location.port
baseUrl = protocol + "//" + hostname + ":" + port
```

价值：

- 本地、局域网、Ngrok 地址都可以复用同一套前端代码。
- 不需要在前端硬编码 `localhost:8080`。

### 5.2 登录状态管理

`auth-manager.js` 负责：

- 登录请求。
- 注册请求。
- `localStorage` 保存当前用户。
- 页面加载时检查登录状态。
- 未登录访问 dashboard 时跳回登录页。
- 已登录访问登录页时跳转到 dashboard。
- 初始化全局状态 `window.globalState`。

全局状态示例：

```js
window.globalState = {
  currentUser: "alice",
  isLoggedIn: true,
  currentPath: "/"
};
```

这种轻量状态管理避免引入前端框架，适合当前项目规模。

### 5.3 文件管理状态机

`file-manager.js` 维护：

- `currentUser`
- `currentPath`
- `selectedFile`
- `selectedFiles`

主要能力：

- 刷新文件列表。
- 渲染文件表格。
- 进入文件夹。
- 返回上级目录。
- 选择文件或目录。
- 批量选择文件。
- 删除。
- 创建文件夹。
- 重命名。

路径更新逻辑：

```text
currentPath = "/"
enter "docs" -> "/docs"
enter "2026" -> "/docs/2026"
goBack -> "/docs"
goBack -> "/"
```

文件类型展示：

- 后端返回 `type = 2` 时视为目录。
- 后端返回 `type = 1` 时视为文件。
- 文件图标根据扩展名映射。

### 5.4 上传队列与拖拽上传

`upload-manager.js` 实现了比单文件上传更复杂的前端队列：

```js
{
  id,
  file,
  status: "waiting" | "uploading" | "completed" | "failed" | "paused",
  progress,
  error
}
```

批量上传流程：

1. 用户多选文件或拖拽多个文件。
2. 文件加入 `uploadQueue`。
3. 若当前未上传，调用 `startBatchUpload()`。
4. 找到第一个 `waiting` 项。
5. 设置为 `uploading`。
6. 调用 `/api/upload`。
7. 成功后标记 `completed`。
8. 失败后标记 `failed`。
9. 500ms 后继续下一个文件。

暂停和继续：

- `pauseUpload()` 只改变队列状态，不中断已发出的 HTTP 请求。
- `resumeUpload()` 恢复后继续查找下一个 waiting 文件。

拖拽上传：

- 监听 `dragover`、`dragleave`、`drop`。
- 单文件直接上传。
- 多文件进入队列。

容错逻辑：

- 前端设置 30 秒超时提示。
- 如果上传请求超时或网络错误，前端会认为“可能已完成”，然后刷新文件列表验证。
- 这是对当前 C Server 上传响应可能不及时的一种用户体验补偿。

### 5.5 下载和批量下载

`download-manager.js` 实现：

- 单文件下载：调用 `/api/download`，拿到 Blob 后创建隐藏 `<a>` 触发浏览器下载。
- 批量下载：把选中文件路径提交给 `/api/batch-download`，下载返回的 ZIP Blob。

关键实现点：

- 下载不刷新页面。
- 使用 `URL.createObjectURL(blob)` 在浏览器内创建临时 URL。
- 下载后调用 `URL.revokeObjectURL(url)` 释放资源。

## 6. eBPF Debugger 实现

eBPF Debugger 位于 `ebpf_debugger/`，它是项目中技术含量最高的观测组件。它不修改 C Server 源码，而是在运行时通过 eBPF 探针采集行为数据。

### 6.1 调试器总体结构

入口 `ebpf_debugger/app.py`：

1. 创建 Flask 应用。
2. 创建 Flask-SocketIO。
3. 初始化三个 Collector：
   - `NetworkCollector`
   - `SyscallCollector`
   - `PerfCollector`
4. 延迟初始化四类 Monitor：
   - `NetworkMonitor`
   - `SyscallTracer`
   - `PerfAnalyzer`
   - `UprobeTracer`
5. 后台线程执行 `monitor_loop()`。
6. 每秒轮询 eBPF perf buffer。
7. 更新历史数据。
8. 通过 WebSocket 推送给浏览器。

数据流：

```text
Kernel / User process
  -> eBPF program
  -> BPF_PERF_OUTPUT
  -> BCC Python callback
  -> Collector aggregation
  -> Flask-SocketIO emit("update")
  -> Browser dashboard charts
```

### 6.2 NetworkMonitor：TCP 连接与吞吐监控

`network_monitor.py` 使用 kprobe / kretprobe 监控 TCP 内核函数：

| 探针 | 作用 |
|---|---|
| `tcp_v4_connect` kprobe | 记录主动连接入口参数和 socket 指针 |
| `tcp_v4_connect` kretprobe | 连接返回后读取 socket 地址信息 |
| `inet_csk_accept` kretprobe | 记录服务端 accept 得到的新 socket |
| `tcp_close` kprobe | 记录连接关闭 |
| `tcp_sendmsg` kprobe | 记录发送字节数 |
| `tcp_recvmsg` kprobe | 记录接收字节数 |

关键技术点：kprobe 与 kretprobe 跨阶段传递数据。

kprobe 入口可以拿到函数参数，例如 `struct sock *sk`；kretprobe 返回时更容易拿到返回值，但入口参数可能已经不可直接使用。代码通过 BPF Hash Map 保存入口阶段的数据：

```text
key = pid_tgid
value = { timestamp, struct sock *sk }
```

返回阶段再通过同一个 `pid_tgid` 查回 socket 指针，读取地址、端口并提交事件。

事件通过：

```c
BPF_PERF_OUTPUT(events);
events.perf_submit(ctx, &event, sizeof(event));
```

发送到用户态，Python `_handle_event()` 转换：

- `event.comm` -> 进程名。
- `event.saddr` / `event.daddr` -> IP 字符串。
- `event.sport` / `event.dport` -> 端口。
- 根据 `event_type` 调用 collector 的 `on_connect()`、`on_send()`、`on_recv()`、`on_close()`。

### 6.3 NetworkCollector：线程安全聚合

`NetworkCollector` 维护：

- `connections`：当前活跃连接。
- `total_bytes_sent`
- `total_bytes_recv`
- `total_connections`
- `throughput_history`
- `connection_count_history`
- `latency_history`
- `recent_events`

所有更新都在 `threading.Lock()` 保护下完成，避免 eBPF 回调线程和 WebSocket 推送线程并发读写状态。

活跃连接 key：

```text
(pid, saddr, daddr, sport, dport)
```

历史数据使用 `deque(maxlen=config.HISTORY_SIZE)`，避免长期运行导致内存无限增长。

### 6.4 SyscallTracer：系统调用延迟和错误率

`syscall_tracer.py` 使用 tracepoint 追踪系统调用。tracepoint 相比 kprobe 更稳定，因为它是内核提供的跟踪点接口，不直接依赖内核函数名。

已覆盖的系统调用包括：

- 文件 I/O：`read`、`write`、`openat`、`close`
- 网络 I/O：`accept4`、`sendto`、`recvfrom`

典型逻辑：

1. `sys_enter_*` tracepoint 记录开始时间。
2. 使用 `pid_tgid` 作为 key 写入 `syscall_start` BPF Hash。
3. `sys_exit_*` tracepoint 查出开始时间。
4. 计算 `duration_ns = now - start_ts`。
5. 读取返回值 `args->ret`。
6. perf submit 到用户态。
7. 删除 map 中的开始时间。

Collector 侧统计：

- 每类系统调用次数。
- 总耗时。
- 平均耗时。
- 错误次数和错误率。
- 按进程聚合的 top syscalls。
- 最近事件。

这可以回答：

- 上传文件时主要发生了多少 `read/write/sendto/recvfrom`？
- 哪些系统调用平均耗时最高？
- 是否存在大量失败的文件或网络调用？

### 6.5 PerfAnalyzer：上下文切换与 CPU 采样

`perf_analyzer.py` 使用两类机制：

1. `sched:sched_switch` tracepoint 统计上下文切换。
2. `attach_perf_event()` 以 10Hz 频率采样 CPU clock。

上下文切换事件中采集：

- `prev_pid`
- `next_pid`
- `prev_comm`
- `next_comm`
- 时间戳

CPU 采样事件中采集：

- 当前 pid。
- 当前 CPU id。
- 当前进程名。
- 时间戳。

采样频率设置为 10Hz 的原因：

- 降低 perf buffer 溢出概率。
- 对调试面板足够实时。
- 避免调试器本身对网盘服务造成过大负担。

### 6.6 UprobeTracer：用户态函数追踪

`uprobe_tracer.py` 对 C Server 可执行文件中的用户态函数打探针。

目标函数：

| 函数 | 观测意义 |
|---|---|
| `handle_client` | 单个客户端请求处理入口，能观测请求处理耗时 |
| `send_directory_tree` | 目录树递归发送，能观测深目录遍历成本 |
| `sha256_string` | 注册/登录密码哈希耗时 |

实现方式：

- 使用 `attach_uprobe()` 挂入口。
- 使用 `attach_uretprobe()` 挂返回。
- 入口记录开始时间。
- 返回时计算函数耗时。
- 提交 `func_events` 到用户态。
- `PerfCollector` 聚合函数调用次数、总耗时、平均耗时、最小耗时、最大耗时。

前提条件：

- `server/server` 可执行文件存在。
- 目标函数符号可被 BCC 找到。若编译优化或符号裁剪导致符号不可见，uprobe 会挂载失败。

### 6.7 WebSocket 实时推送

调试器不是靠前端频繁轮询，而是通过 SocketIO 推送：

```text
monitor_loop()
  -> collector.update_history()
  -> socketio.emit("update", {
       network,
       syscall,
       perf
     })
```

优势：

- 前端能实时更新图表。
- 后端统一控制采样周期。
- REST API 仍保留 `/api/network`、`/api/syscall`、`/api/perf`、`/api/all` 供调试或脚本获取快照。

## 7. 构建、启动与运行编排

### 7.1 Makefile

`Makefile` 定义：

- `server/server`：链接 SQLite、OpenSSL、pthread。
- `client/client`：链接 Readline。
- `debug`：追加 `-g -DDEBUG`。
- `clean`：清理可执行文件和对象文件。
- `distclean`：额外清理 `netdisk.db` 和 `netdisk_data`。

链接关系：

```text
server/src/main.c -> server/server
  libs: sqlite3, ssl, crypto, pthread

client/src/main.c -> client/client
  libs: readline
```

### 7.2 start.sh

一键启动脚本流程：

1. 检查 gcc。
2. 检查 python3。
3. 检查 Flask 相关 Python 包，缺失时安装。
4. 若 server/client 不存在，执行 `make clean && bear -- make`。
5. 创建 `netdisk_data`。
6. 清理 9000 和 8080 端口占用。
7. 后台启动 C Server。
8. 使用 `nc -z 127.0.0.1 9000` 验证 C Server。
9. 后台启动 Flask Web Server。
10. 使用 `curl http://localhost:8080` 验证 Web Server。
11. 打印本地、局域网和 Ngrok 访问信息。
12. 循环监测两个服务进程是否存活。

### 7.3 stop.sh

停止脚本流程：

- 查找占用 9000 的进程并 kill。
- 查找占用 8080 的进程并 kill。
- `pkill -f "python3 app.py"` 清理 Flask。
- `pkill -f "server/server"` 清理 C Server。

当前限制：

- 使用 `kill -9` 较强硬，服务没有机会优雅关闭。
- `pkill -f "python3 app.py"` 可能误杀其他同名应用。

### 7.4 eBPF 调试器启动

`ebpf_debugger/start_debugger.sh`：

- 检查是否 root。
- 检查 Python 包：Flask、Flask-SocketIO、eventlet、psutil。
- 检查 BCC。
- 可选安装 BCC 和 Linux headers。
- 设置 `PYTHONPATH`。
- 启动 `python3 app.py`。

eBPF 需要 root 权限和内核支持；无 BCC 时调试器会进入无 eBPF 数据模式，Web 面板仍可启动。

## 8. 关键工程亮点

### 8.1 C 核心服务与 Web 界面解耦

项目没有把所有逻辑都写在 Flask 中，而是将核心数据操作放在 C Server 中。Web 层只是协议适配器。这种架构的好处：

- CLI 和 Web 共用同一套服务端能力。
- C Server 可以独立测试和部署。
- Web 层后续可以替换成其他语言或框架。

### 8.2 自定义二进制协议跨语言复用

协议足够简单，但覆盖了认证、文件流、目录元数据、目录树等需求。C 端和 Python 端通过统一的长度前缀字符串、网络字节序整数完成互通。

关键难点在于：

- C 端处理动态长度字符串。
- Python 端使用 `struct.pack/unpack` 严格匹配字节序。
- 64 位文件大小跨语言传输。
- 目录列表复合结构解析。

### 8.3 文件传输使用流式设计

C Server 对上传和下载都采用 4096 字节缓冲循环，避免服务端一次性把文件读入内存。虽然 Web 桥接层当前仍有内存化上传/下载问题，但核心 C 服务本身具备流式处理基础。

### 8.4 CLI 具备真实远端补全能力

普通命令行客户端往往只补全本地文件，而本项目 CLI 会向服务端请求当前目录条目，再根据命令上下文补全远端目录或文件。这是交互体验上的一个亮点。

### 8.5 eBPF 非侵入式观测

调试器能在不改动业务代码的情况下观察：

- TCP 连接生命周期。
- socket 发送接收字节数。
- 文件和网络系统调用耗时。
- 上下文切换。
- CPU 采样。
- C Server 用户态函数耗时。

这让项目不仅“能运行”，还具备运行时分析能力，便于定位性能瓶颈和协议问题。

### 8.6 Web 前端模块化状态管理

前端没有使用大型框架，但通过 `AuthManager`、`FileManager`、`UploadManager`、`DownloadManager` 分离职责。每个模块维护自己的状态，同时通过 `window.globalState` 做轻量协作。

## 9. 当前实现中的技术风险与不一致点

本节记录当前代码中值得特别注意的问题。这些不影响本文档描述项目设计，但会影响系统稳定性、安全性或部分功能可用性。

### 9.1 TCP `read()` / `write()` 未保证完整读写

当前大量代码使用：

```c
read(fd, buf, len);
write(fd, buf, len);
```

但 TCP 是字节流，单次调用可能只读到或写出部分数据。更稳妥的方式是封装：

```text
read_full(fd, buf, len)
write_full(fd, buf, len)
```

直到累计字节数达到目标长度或发生错误。

影响：

- 大文件传输、网络抖动或高并发时更容易出错。
- Python `sock.recv(n)` 同样不保证一次读满，也应封装 `recv_exact(n)`。

### 9.2 CLI 上传与服务端协议不一致

C Server `U` 命令要求字段顺序：

```text
'U' + username + target_dir + filename + file_size + file_content
```

当前 Web 上传实现按此协议发送了 file_size。

但 CLI `upload_file()` 当前在发送 filename 后直接发送文件内容，没有发送 file_size。服务端会把文件内容的前 8 字节误当作 `size_high` 和 `size_low`，导致 CLI 上传逻辑不可靠。

建议修复：

- 在 CLI `upload_file()` 发送文件内容前补充发送高低 32 位文件大小。
- 上传结束后读取服务端 1 字节响应，而不是直接关闭连接并提示完成。

### 9.3 Web 下载与服务端 `D` 协议不一致

C Server `D` 命令期望：

```text
'D' + username + current_dir + filename
```

当前 Flask `/api/download` 只发送：

```text
'D' + username + filename
```

这会导致 C Server 把 filename 当作 current_dir 读取，后续再等待 filename 字段，出现协议错位或阻塞。

批量下载中也存在相同问题。

建议修复：

- Flask 根据 `filePath` 拆出 `current_dir` 和 `filename`。
- 按 C Server 协议依次发送 username、current_dir、filename。

### 9.4 Web 删除与服务端 `X` 协议不一致

C Server `X` 命令期望：

```text
'X' + username + current_dir + filename
```

当前 Flask `/api/delete` 发送：

```text
'X' + username + file_path
```

同样会造成协议错位。

建议修复：

- 从 `filePath` 拆出目录和 basename。
- 发送 username、current_dir、filename。

### 9.5 删除目录使用 `system("rm -rf ...")`

服务端删除非空目录时使用 shell 命令兜底。即使路径被双引号包裹，也仍然不应把用户可影响的路径传给 shell。

建议：

- 实现 C 语言递归删除函数。
- 对路径做 canonicalize，确保最终路径仍位于 `netdisk_data/<username>/` 下。

### 9.6 路径安全边界不足

当前路径构造分散在多个命令分支中，没有统一函数保证：

- 去除 `..`。
- 处理重复 `/`。
- 禁止绝对路径逃逸。
- 校验最终真实路径在用户根目录下。

建议：

- 新增统一 `build_user_path(username, user_path, out, out_size)`。
- 使用 `realpath()` 或手写规范化逻辑。
- 对不存在但即将创建的路径单独处理父目录。

### 9.7 Web 配置中存在敏感信息硬编码

`gui_examples/api/config.py` 中包含 DuckDNS token 和 Flask secret 示例值。生产环境应使用环境变量注入，不应提交真实 token。

建议：

- 使用 `os.environ.get()` 读取。
- 提供 `.env.example`。
- 将真实密钥加入 `.gitignore` 管理。

### 9.8 密码存储不适合生产

当前使用裸 SHA-256：

```text
password_hash = SHA256(password)
```

建议改为：

- Argon2id、bcrypt 或 PBKDF2。
- 每用户随机 salt。
- 可配置迭代成本。

### 9.9 服务停止方式较粗暴

`stop.sh` 和 `start.sh` 使用 `kill -9` 清理端口。这样可能导致：

- SQLite 未正常关闭。
- 正在上传的文件残留半截。
- 日志和资源来不及 flush。

建议：

- 优先发送 SIGTERM。
- 服务端捕获信号后停止 accept，等待当前连接处理完成。
- 超时后再 SIGKILL。

## 10. 建议的后续演进路线

### 10.1 协议层

- 增加协议版本号和魔数。
- 为每个请求增加统一 header：

```text
magic(4) + version(1) + command(1) + payload_length(4)
```

- 响应使用统一结构：

```text
status(1) + error_code(4) + message_length(4) + message
```

- 封装 C 端 `read_full/write_full`。
- 封装 Python 端 `recv_exact/send_all`。

### 10.2 安全层

- 用户名限制为 `[a-zA-Z0-9_-]`，服务端也必须校验。
- 所有路径经过统一规范化。
- 禁止目录穿越。
- 删除目录改为安全递归删除。
- 密码改为带 salt 的 KDF。
- 外网访问时引入 HTTPS 或反向代理 TLS。

### 10.3 文件层

- 引入文件元数据库表，记录文件大小、路径、所有者、mtime、hash。
- 上传使用临时文件：

```text
filename.part -> 校验成功 -> rename(filename)
```

- 支持断点续传。
- 支持文件 hash 校验。

### 10.4 Web 层

- 上传和下载改为真正流式转发，避免 Flask 内存保存完整文件。
- 批量 ZIP 可使用流式 ZIP 响应。
- API 增加认证 token，而不是仅传 username。
- 前端对文件名进行 HTML 转义，避免特殊文件名造成 DOM 注入。

### 10.5 eBPF 层

- 增加按端口过滤，减少无关系统流量。
- 对 perf buffer lost events 做统计。
- uprobe 增加更多业务函数，例如上传、下载、mkdir、rename 分支函数。
- 将 eBPF 面板和业务 Web 面板做只读鉴权隔离。

## 11. 关键文件索引

| 文件 | 说明 |
|---|---|
| `server/src/main.c` | C Server 主实现，包含协议处理、认证、文件操作、目录树、线程模型 |
| `client/src/main.c` | CLI Client，包含 Readline、命令解析、补全、上传下载 |
| `gui_examples/app.py` | Flask Web Bridge 入口 |
| `gui_examples/api/c_server_client.py` | Python 到 C Server 的协议封装 |
| `gui_examples/api/auth_routes.py` | 登录注册 API |
| `gui_examples/api/file_routes.py` | 文件管理 API |
| `gui_examples/js/auth-manager.js` | 前端登录状态管理 |
| `gui_examples/js/file-manager.js` | 前端文件列表、路径、删除、重命名 |
| `gui_examples/js/upload-manager.js` | 上传队列、拖拽上传、暂停继续 |
| `gui_examples/js/download-manager.js` | 单文件和批量下载 |
| `ebpf_debugger/app.py` | eBPF 调试器 WebSocket 主循环 |
| `ebpf_debugger/bpf_programs/network_monitor.py` | TCP 连接和吞吐 eBPF 监控 |
| `ebpf_debugger/bpf_programs/syscall_tracer.py` | 系统调用 tracepoint 追踪 |
| `ebpf_debugger/bpf_programs/perf_analyzer.py` | CPU 采样和上下文切换 |
| `ebpf_debugger/bpf_programs/uprobe_tracer.py` | C Server 用户态函数 uprobe |
| `ebpf_debugger/collectors/*.py` | eBPF 事件聚合、历史数据和统计 |
| `Makefile` | C Server 和 CLI Client 构建规则 |
| `start.sh` | 一键启动主服务 |
| `stop.sh` | 停止主服务 |
| `ebpf_debugger/start_debugger.sh` | 启动 eBPF 调试器 |

## 12. 总结

这个项目的核心技术价值不只是“能上传下载文件”，而是将多个层次的系统编程能力组合在一起：

- 用 C 和 POSIX socket 实现核心 TCP 文件服务。
- 用 SQLite 和 SHA-256 完成基础用户认证。
- 用自定义二进制协议承载认证、文件流和目录元数据。
- 用 Flask 将 HTTP/Web 世界桥接到 C 服务端。
- 用前端模块化管理登录、文件路径、上传队列和批量下载。
- 用 eBPF/BCC 在内核和用户态对系统运行行为做非侵入式观测。

当前代码已经具备清晰的分层和较多工程亮点，但协议一致性、路径安全、完整读写、密码存储和生产级密钥管理仍需要进一步加固。若后续优先修复这些基础问题，再继续扩展断点续传、权限模型和观测指标，这个项目可以从课程级网盘演进为更稳健的小型文件服务系统。
