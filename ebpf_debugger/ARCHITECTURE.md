# eBPF调试器架构详解

## 目录
1. [整体架构](#整体架构)
2. [核心组件](#核心组件)
3. [数据流向](#数据流向)
4. [探针类型](#探针类型)
5. [实现要点](#实现要点)
6. [Web前端](#web前端)

eBPF 是一种在内核空间运行沙箱程序的技术，无需修改内核源码。更多基础知识见 [eBPF文档](https://ebpf.io)。

---

## 整体架构

```
┌─────────────────────────────────────────────────────────────────┐
│                        Web浏览器                                 │
│                   http://localhost:8090                          │
└────────────────────┬────────────────────────────────────────────┘
                     │ HTTP + WebSocket
                     ↓
┌─────────────────────────────────────────────────────────────────┐
│                     Flask Web服务器                              │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐         │
│  │   app.py     │  │  api routes  │  │  WebSocket   │         │
│  │  (主应用)     │  │  (REST API)  │  │  (实时推送)   │         │
│  └──────────────┘  └──────────────┘  └──────────────┘         │
└────────────────────┬────────────────────────────────────────────┘
                     │
                     ↓
┌─────────────────────────────────────────────────────────────────┐
│                     数据收集器层                                  │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐         │
│  │  Network     │  │   Syscall    │  │     Perf     │         │
│  │  Collector   │  │  Collector   │  │  Collector   │         │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘         │
└─────────┼──────────────────┼──────────────────┼─────────────────┘
          │                  │                  │
          ↓                  ↓                  ↓
┌─────────────────────────────────────────────────────────────────┐
│                     eBPF监控器层                                  │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐         │
│  │   Network    │  │   Syscall    │  │     Perf     │         │
│  │   Monitor    │  │   Tracer     │  │   Analyzer   │         │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘         │
│         │ BCC Python API   │                  │                  │
│         ↓                  ↓                  ↓                  │
│  ┌─────────────────────────────────────────────────────┐       │
│  │          eBPF C程序 (内核空间运行)                    │       │
│  │  • kprobe/kretprobe - 内核函数探针                   │       │
│  │  • uprobe/uretprobe - 用户态函数探针                 │       │
│  │  • tracepoint - 内核跟踪点                           │       │
│  └─────────────────────────────────────────────────────┘       │
└────────────────────┬────────────────────────────────────────────┘
                     │
                     ↓
┌─────────────────────────────────────────────────────────────────┐
│                      Linux内核                                   │
│  • TCP连接 (tcp_v4_connect, inet_csk_accept, tcp_close)         │
│  • 数据传输 (tcp_sendmsg, tcp_recvmsg)                           │
│  • 系统调用 (sys_enter/sys_exit)                                 │
│  • CPU调度 (cpu-clock事件)                                       │
│  • 用户态函数 (handle_client, send_directory_tree, ...)         │
└─────────────────────────────────────────────────────────────────┘
```

---

## 核心组件

### 1. Flask Web应用 (`app.py`)

初始化收集器和监控器，启动 WebSocket 实时推送循环：

```python
network_collector = NetworkCollector()
syscall_collector = SyscallCollector()
perf_collector = PerfCollector()

monitors = {
    'network': NetworkMonitor(network_collector),
    'syscall': SyscallTracer(syscall_collector),
    'perf': PerfAnalyzer(perf_collector),
    'uprobe': UprobeTracer(perf_collector)
}

def monitor_loop():
    while running:
        for monitor in monitors.values():
            monitor.poll(timeout=10)
        network_collector.update_history()
        socketio.emit('update', {
            'network': network_collector.get_stats(),
            'syscall': syscall_collector.get_stats(),
            'perf': perf_collector.get_stats()
        })
        time.sleep(1)
```

### 2. 数据收集器 (`collectors/`)

聚合 eBPF 原始事件，维护统计数据：

```python
class NetworkCollector:
    def __init__(self):
        self.connections = {}
        self.throughput_history = []
        self.recent_events = []

    def on_connect(self, pid, comm, saddr, daddr, sport, dport):
        key = (pid, saddr, daddr, sport, dport)
        self.connections[key] = ConnectionInfo(...)

    def on_send(self, pid, saddr, daddr, sport, dport, size):
        self.connections[key].bytes_sent += size

    def get_stats(self):
        return {
            'active_connections': len(self.connections),
            'total_bytes_sent': self.total_bytes_sent,
            'connections': list(self.connections.values())
        }
```

- **SyscallCollector**: 统计系统调用次数与延迟
- **PerfCollector**: CPU采样统计、用户态函数追踪

### 3. eBPF监控器 (`bpf_programs/`)

每个监控器包含：内核空间 C 程序 + Python 控制器。

#### NetworkMonitor

监控 TCP 连接建立/关闭和数据发送/接收。关键设计：在 kprobe 入口保存 socket 指针到 BPF map，在 kretprobe 返回时从 map 取出读取地址信息。

```c
// 入口保存socket
int trace_tcp_connect(struct pt_regs *ctx, struct sock *sk) {
    struct connect_info_t info = { .ts = bpf_ktime_get_ns(), .sk = sk };
    connect_info.update(&pid_tgid, &info);
    return 0;
}

// 返回时读取地址
int trace_tcp_connect_ret(struct pt_regs *ctx) {
    struct connect_info_t *info = connect_info.lookup(&pid_tgid);
    get_sock_info(info->sk, &event.saddr, &event.daddr, ...);
    events.perf_submit(ctx, &event, sizeof(event));
    return 0;
}
```

Python 控制器通过 BCC API 附加探针：

```python
self.bpf.attach_kprobe(event="tcp_v4_connect", fn_name="trace_tcp_connect")
self.bpf.attach_kretprobe(event="tcp_v4_connect", fn_name="trace_tcp_connect_ret")
self.bpf["events"].open_perf_buffer(self._handle_event)
```

监控的内核函数：`tcp_v4_connect`, `inet_csk_accept`, `tcp_close`, `tcp_sendmsg`, `tcp_recvmsg`

#### SyscallTracer

使用 tracepoint（比 kprobe 更稳定）追踪所有系统调用：

```c
TRACEPOINT_PROBE(raw_syscalls, sys_enter) {
    u64 ts = bpf_ktime_get_ns();
    syscall_start.update(&pid_tgid, &ts);
    return 0;
}

TRACEPOINT_PROBE(raw_syscalls, sys_exit) {
    u64 *start_ts = syscall_start.lookup(&pid_tgid);
    u64 delta = bpf_ktime_get_ns() - *start_ts;
    events.perf_submit(ctx, &event, sizeof(event));
    return 0;
}
```

#### PerfAnalyzer

CPU 采样（10Hz 避免缓冲区溢出）：

```c
int on_cpu_sample(struct bpf_perf_event_data *ctx) {
    u32 pid = bpf_get_current_pid_tgid() >> 32;
    bpf_get_current_comm(&event.comm, sizeof(event.comm));
    events.perf_submit(ctx, &event, sizeof(event));
    return 0;
}
```

#### UprobeTracer

追踪用户态函数（需要可执行文件保留符号表）：

```python
self.bpf.attach_uprobe(name=server_path, sym="handle_client",
    fn_name="trace_handle_client_enter")
self.bpf.attach_uretprobe(name=server_path, sym="handle_client",
    fn_name="trace_handle_client_exit")
```

---

## 数据流向

以 TCP 连接为例的 11 步完整流程：

```
1. [内核] 应用程序调用 connect()
2. [内核] tcp_v4_connect() 执行
3. [eBPF] trace_tcp_connect() 触发 → 保存socket指针到BPF map
4. [内核] tcp_v4_connect() 返回
5. [eBPF] trace_tcp_connect_ret() 触发 → 从map取socket → 读取地址 → perf_submit()
6. [用户空间] NetworkMonitor.poll() → perf_buffer_poll()
7. [Python] _handle_event() 回调 → 解析事件, inet_ntoa() 转换IP
8. [Python] NetworkCollector.on_connect() → 更新统计数据
9. [Flask] monitor_loop() → socketio.emit('update', data)
10. [WebSocket] 数据推送到浏览器
11. [前端] JavaScript 接收 → 更新图表和表格
```

---

## 探针类型

| 探针类型 | 挂载点 | 稳定性 | 适用场景 |
|---------|--------|--------|---------|
| kprobe | 内核函数入口 | 依赖函数名 | 快速原型开发 |
| kretprobe | 内核函数返回 | 依赖函数名 | 获取返回值 |
| tracepoint | 内核预定义点 | 稳定API | 生产环境 |
| uprobe | 用户态函数 | 依赖符号表 | 应用程序追踪 |

### kprobe/kretprobe 参数传递

kprobe 可访问函数参数，kretprobe 只能访问返回值。跨探针传递数据需通过 BPF map：

```c
// 入口: 保存参数
int trace_enter(struct pt_regs *ctx, struct sock *sk) {
    info.sk = sk;
    connect_info.update(&pid_tgid, &info);
}

// 返回: 从map取出
int trace_exit(struct pt_regs *ctx) {
    struct info_t *info = connect_info.lookup(&pid_tgid);
    use(info->sk);
}
```

### BPF Maps

```c
BPF_HASH(connect_info, u64, struct connect_info_t);   // 键值存储
BPF_PERF_OUTPUT(events);                               // 向用户空间发送事件

connect_info.update(&key, &value);   // 插入
connect_info.lookup(&key);           // 查找
events.perf_submit(ctx, &data, size); // 推送事件
```

---

## 实现要点

### 采样频率调优

初始使用 99Hz 采样导致大量丢样本：`Possibly lost 152 samples`。原因是 CPU 采样频率过高导致 perf buffer 溢出。降低到 10Hz 后解决，开销 < 5% CPU。

### 地址显示 0.0.0.0 的问题

表现为 socket 地址显示为 `0.0.0.0:00`。根因是 TCP 连接建立是异步的，connect() 返回时地址可能未就绪。修复方案：在 kprobe 入口保存 socket 指针，在 kretprobe 返回时从 map 取出读取地址；对于仍需在 sendmsg/recvmsg 阶段读取的情况，增加数据传输时机的地址捕获。

### Root 权限

eBPF 程序加载需要 `CAP_BPF` 或 `CAP_SYS_ADMIN`，启动需 `sudo python3 app.py`。

### 调试方法

```python
print(list(self.bpf['connect_info'].items()))  # 打印 BPF map 内容
```
```bash
sudo bpftool prog list    # 查看加载的 eBPF 程序
sudo bpftool map list     # 查看 BPF map
```

### 关键注意

- kretprobe 中不能访问函数参数（参数已销毁）
- CPU 采样频率不宜超过 10-20Hz
- 优先使用 tracepoint 而非 kprobe（跨内核版本更稳定）
- uprobe 需要二进制保留符号表

---

## Web前端

技术选型：Chart.js 图表 + Socket.IO WebSocket + 原生 JavaScript（无框架依赖）。

### 实时图表 (`charts.js`)

```javascript
const throughputChart = new Chart(ctx, {
    type: 'line',
    data: {
        labels: [],
        datasets: [{
            label: '发送', data: [], borderColor: '#10b981', fill: false
        }, {
            label: '接收', data: [], borderColor: '#3b82f6', fill: false
        }]
    },
    options: {
        responsive: true,
        animation: { duration: 0 },  // 禁用动画提高实时性能
        scales: { y: { beginAtZero: true } }
    }
});
```

### WebSocket 实时通信 (`realtime.js`)

```javascript
const socket = io();

socket.on('update', (data) => {
    updateNetworkCharts(data.network);
    updateSyscallCharts(data.syscall);
    updatePerfCharts(data.perf);
});
```

### 国际化 (`i18n.js`)

支持中英文切换，通过 `localStorage` 持久化语言偏好，`data-i18n` 属性标记可翻译元素。
