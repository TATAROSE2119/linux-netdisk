# eBPF调试器架构详解

## 📋 目录
1. [什么是eBPF](#什么是ebpf)
2. [整体架构](#整体架构)
3. [核心组件](#核心组件)
4. [数据流向](#数据流向)
5. [工作原理](#工作原理)

---

## 什么是eBPF

**eBPF (Extended Berkeley Packet Filter)** 是Linux内核的一项革命性技术，允许在内核空间运行沙箱程序，无需修改内核源码或加载内核模块。

### 核心特点
- **安全性**: 程序在沙箱环境中运行，经过验证器检查
- **高性能**: 直接在内核空间执行，开销极小
- **动态性**: 无需重启系统，可动态加载/卸载
- **可观测性**: 可以监控几乎所有内核事件

### 应用场景
- 网络流量监控和过滤
- 系统调用追踪
- 性能分析和CPU采样
- 安全审计

---

## 整体架构

```
┌─────────────────────────────────────────────────────────────────┐
│                         Web浏览器                                │
│                    http://localhost:8090                         │
└────────────────────┬────────────────────────────────────────────┘
                     │ HTTP + WebSocket
                     ↓
┌─────────────────────────────────────────────────────────────────┐
│                      Flask Web服务器                             │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐         │
│  │   app.py     │  │  api routes  │  │  WebSocket   │         │
│  │  (主应用)     │  │  (REST API)  │  │  (实时推送)   │         │
│  └──────────────┘  └──────────────┘  └──────────────┘         │
└────────────────────┬────────────────────────────────────────────┘
                     │
                     ↓
┌─────────────────────────────────────────────────────────────────┐
│                      数据收集器层                                 │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐         │
│  │  Network     │  │   Syscall    │  │     Perf     │         │
│  │  Collector   │  │  Collector   │  │  Collector   │         │
│  │ (网络数据)    │  │ (系统调用)    │  │ (性能数据)    │         │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘         │
│         │                  │                  │                  │
└─────────┼──────────────────┼──────────────────┼─────────────────┘
          │                  │                  │
          ↓                  ↓                  ↓
┌─────────────────────────────────────────────────────────────────┐
│                      eBPF监控器层                                │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐         │
│  │   Network    │  │   Syscall    │  │     Perf     │         │
│  │   Monitor    │  │   Tracer     │  │   Analyzer   │         │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘         │
│         │                  │                  │                  │
│         │ BCC Python API   │                  │                  │
│         ↓                  ↓                  ↓                  │
│  ┌─────────────────────────────────────────────────────┐       │
│  │           eBPF C程序 (内核空间运行)                   │       │
│  │  • kprobe/kretprobe - 内核函数探针                   │       │
│  │  • uprobe/uretprobe - 用户态函数探针                 │       │
│  │  • tracepoint - 内核跟踪点                           │       │
│  └─────────────────────────────────────────────────────┘       │
└────────────────────┬────────────────────────────────────────────┘
                     │ 监控内核事件
                     ↓
┌─────────────────────────────────────────────────────────────────┐
│                       Linux内核                                  │
│  • TCP连接 (tcp_v4_connect, inet_csk_accept, tcp_close)         │
│  • 数据传输 (tcp_sendmsg, tcp_recvmsg)                           │
│  • 系统调用 (sys_enter_* / sys_exit_*)                           │
│  • CPU调度 (cpu-clock事件)                                       │
│  • 用户态函数 (handle_client, send_directory_tree, ...)         │
└─────────────────────────────────────────────────────────────────┘
```

---

## 核心组件

### 1. Flask Web应用 (`app.py`)

**作用**: 提供Web界面和API服务

**主要功能**:
- 启动Web服务器 (端口8090)
- 初始化并管理4个eBPF监控器
- 通过WebSocket实时推送数据
- 提供REST API查询统计数据

**关键代码**:
```python
# 数据收集器实例
network_collector = NetworkCollector()
syscall_collector = SyscallCollector()
perf_collector = PerfCollector()

# 监控器字典
monitors = {
    'network': NetworkMonitor(network_collector),
    'syscall': SyscallTracer(syscall_collector),
    'perf': PerfAnalyzer(perf_collector),
    'uprobe': UprobeTracer(perf_collector)
}

# 监控主循环 (每秒执行)
def monitor_loop():
    while running:
        # 1. 轮询eBPF事件
        for monitor in monitors.values():
            monitor.poll(timeout=10)

        # 2. 更新历史数据
        network_collector.update_history()

        # 3. 推送到前端
        socketio.emit('update', {
            'network': network_collector.get_stats(),
            ...
        })
        time.sleep(1)  # 每秒采样一次
```

---

### 2. 数据收集器 (`collectors/`)

**作用**: 聚合和存储从eBPF收到的原始事件

#### NetworkCollector (网络数据收集器)
```python
class NetworkCollector:
    def __init__(self):
        self.connections = {}          # 当前活跃连接
        self.throughput_history = []   # 吞吐量历史
        self.recent_events = []        # 最近事件

    def on_connect(self, pid, comm, saddr, daddr, sport, dport):
        """处理新连接事件"""
        # 记录连接信息
        key = (pid, saddr, daddr, sport, dport)
        self.connections[key] = ConnectionInfo(...)

    def on_send(self, pid, saddr, daddr, sport, dport, size):
        """处理发送数据事件"""
        # 累加发送字节数
        self.connections[key].bytes_sent += size

    def get_stats(self):
        """返回统计数据给前端"""
        return {
            'active_connections': len(self.connections),
            'total_bytes_sent': self.total_bytes_sent,
            'connections': [...]
        }
```

#### SyscallCollector (系统调用收集器)
- 统计各类系统调用次数 (read, write, open, close, ...)
- 记录系统调用延迟

#### PerfCollector (性能数据收集器)
- CPU使用率采样
- 函数调用统计
- 用户态函数追踪结果

---

### 3. eBPF监控器 (`bpf_programs/`)

每个监控器包含两部分：
1. **eBPF C程序** (字符串形式) - 运行在内核空间
2. **Python控制器** - 加载eBPF程序并处理事件

#### 3.1 NetworkMonitor (网络监控器)

**监控内容**:
- TCP连接建立/关闭
- 数据发送/接收

**eBPF程序结构**:
```c
// 事件数据结构
struct event_t {
    u32 pid;
    char comm[16];      // 进程名
    u8 event_type;      // CONNECT/ACCEPT/CLOSE/SEND/RECV
    u32 saddr, daddr;   // 源/目标IP
    u16 sport, dport;   // 源/目标端口
    u64 bytes;          // 传输字节数
};

// kprobe探针函数
int trace_tcp_connect(struct pt_regs *ctx, struct sock *sk) {
    // 保存socket信息
    struct connect_info_t info = {
        .ts = bpf_ktime_get_ns(),
        .sk = sk
    };
    connect_info.update(&pid_tgid, &info);
    return 0;
}

// kretprobe探针函数 (函数返回时触发)
int trace_tcp_connect_ret(struct pt_regs *ctx) {
    // 获取之前保存的socket
    struct connect_info_t *info = connect_info.lookup(&pid_tgid);

    // 读取socket地址信息
    get_sock_info(info->sk, &event.saddr, &event.daddr, ...);

    // 通过perf buffer发送事件到用户空间
    events.perf_submit(ctx, &event, sizeof(event));
    return 0;
}
```

**Python控制器**:
```python
class NetworkMonitor:
    def start(self):
        # 1. 编译eBPF程序
        self.bpf = BPF(text=BPF_PROGRAM)

        # 2. 附加探针到内核函数
        self.bpf.attach_kprobe(event="tcp_v4_connect", fn_name="trace_tcp_connect")
        self.bpf.attach_kretprobe(event="tcp_v4_connect", fn_name="trace_tcp_connect_ret")

        # 3. 设置事件回调
        self.bpf["events"].open_perf_buffer(self._handle_event)

    def poll(self, timeout=100):
        """轮询事件 (非阻塞)"""
        self.bpf.perf_buffer_poll(timeout=timeout)

    def _handle_event(self, cpu, data, size):
        """处理收到的事件"""
        event = self.bpf["events"].event(data)
        # 解析IP地址
        saddr = inet_ntoa(event.saddr)
        daddr = inet_ntoa(event.daddr)
        # 调用收集器方法
        self.collector.on_connect(event.pid, event.comm, saddr, daddr, ...)
```

**监控的内核函数**:
- `tcp_v4_connect` - 主动发起TCP连接
- `inet_csk_accept` - 接受TCP连接
- `tcp_close` - 关闭TCP连接
- `tcp_sendmsg` - 发送TCP数据
- `tcp_recvmsg` - 接收TCP数据

---

#### 3.2 SyscallTracer (系统调用追踪器)

**监控内容**:
- 所有系统调用的调用次数
- 系统调用延迟

**使用tracepoint**:
```c
// tracepoint探针 (更稳定,不依赖内核版本)
TRACEPOINT_PROBE(raw_syscalls, sys_enter) {
    u64 id = args->id;  // 系统调用号
    u64 ts = bpf_ktime_get_ns();

    // 记录开始时间
    syscall_start.update(&pid_tgid, &ts);
    return 0;
}

TRACEPOINT_PROBE(raw_syscalls, sys_exit) {
    // 计算延迟
    u64 delta = bpf_ktime_get_ns() - *start_ts;

    // 发送事件
    events.perf_submit(ctx, &event, sizeof(event));
    return 0;
}
```

---

#### 3.3 PerfAnalyzer (性能分析器)

**监控内容**:
- CPU使用率 (通过采样)
- 热点函数统计

**使用perf_event**:
```python
# 每秒采样10次 (10Hz)
self.bpf.attach_perf_event(
    ev_type=PerfType.SOFTWARE,
    ev_config=PerfSWConfig.CPU_CLOCK,
    fn_name="on_cpu_sample",
    sample_freq=10
)
```

```c
int on_cpu_sample(struct bpf_perf_event_data *ctx) {
    u64 pid_tgid = bpf_get_current_pid_tgid();
    u32 pid = pid_tgid >> 32;

    // 获取进程名
    char comm[16];
    bpf_get_current_comm(&comm, sizeof(comm));

    // 记录CPU采样点
    struct event_t event = {
        .pid = pid,
        .comm = comm,
        .timestamp = bpf_ktime_get_ns()
    };
    events.perf_submit(ctx, &event, sizeof(event));
    return 0;
}
```

---

#### 3.4 UprobeTracer (用户态函数追踪器)

**监控内容**:
- 用户空间函数调用 (如 `handle_client`, `send_directory_tree`)
- 函数调用次数和延迟

**使用uprobe**:
```python
# 附加到可执行文件的特定函数
self.bpf.attach_uprobe(
    name=server_path,           # 可执行文件路径
    sym="handle_client",        # 函数名
    fn_name="trace_handle_client_enter"
)

self.bpf.attach_uretprobe(
    name=server_path,
    sym="handle_client",
    fn_name="trace_handle_client_exit"
)
```

```c
int trace_handle_client_enter(struct pt_regs *ctx) {
    u64 ts = bpf_ktime_get_ns();
    func_start.update(&pid_tgid, &ts);
    return 0;
}

int trace_handle_client_exit(struct pt_regs *ctx) {
    u64 *start_ts = func_start.lookup(&pid_tgid);
    if (!start_ts) return 0;

    u64 delta = bpf_ktime_get_ns() - *start_ts;

    // 记录函数执行时间
    struct event_t event = {
        .function_name = "handle_client",
        .duration_ns = delta
    };
    events.perf_submit(ctx, &event, sizeof(event));
    return 0;
}
```

---

## 数据流向

### 完整流程示例：监控一次TCP连接

```
1. [内核] 应用程序调用 connect()
         ↓
2. [内核] tcp_v4_connect() 执行
         ↓
3. [eBPF] trace_tcp_connect() 触发 (kprobe入口探针)
         → 保存socket指针和时间戳
         ↓
4. [内核] tcp_v4_connect() 返回
         ↓
5. [eBPF] trace_tcp_connect_ret() 触发 (kretprobe返回探针)
         → 读取socket的源/目标地址
         → 构造event_t结构体
         → perf_submit() 发送到perf buffer
         ↓
6. [用户空间] NetworkMonitor.poll() 读取perf buffer
         → self.bpf.perf_buffer_poll()
         ↓
7. [Python] _handle_event() 回调触发
         → 解析event_t数据
         → inet_ntoa(event.saddr) 转换IP
         ↓
8. [Python] NetworkCollector.on_connect()
         → 保存连接信息到字典
         → 更新统计数据
         ↓
9. [Flask] monitor_loop() 定期执行
         → network_collector.get_stats() 获取数据
         → socketio.emit('update', data) 推送
         ↓
10. [WebSocket] 数据发送到浏览器
         ↓
11. [前端] JavaScript接收数据
         → 更新图表
         → 刷新表格
```

---

## 工作原理

### 探针类型对比

| 探针类型 | 挂载点 | 稳定性 | 适用场景 |
|---------|--------|--------|---------|
| **kprobe** | 内核函数入口 | 依赖函数名 | 快速原型开发 |
| **kretprobe** | 内核函数返回 | 依赖函数名 | 获取返回值 |
| **tracepoint** | 内核预定义点 | 稳定API | 生产环境 |
| **uprobe** | 用户态函数 | 依赖符号表 | 应用程序追踪 |

### kprobe vs kretprobe 使用场景

**问题**: 为什么`trace_tcp_connect_ret`需要在入口保存socket？

因为：
- **kprobe** (入口探针): 可以访问函数参数 `struct sock *sk`
- **kretprobe** (返回探针): 只能访问返回值，参数已销毁

**解决方案**:
1. 在kprobe中保存socket指针到BPF map
2. 在kretprobe中从map查找socket
3. 读取socket的地址信息

```c
// 入口: 保存参数
int trace_tcp_connect(struct pt_regs *ctx, struct sock *sk) {
    info.sk = sk;  // 保存socket指针
    connect_info.update(&pid_tgid, &info);
}

// 返回: 使用保存的参数
int trace_tcp_connect_ret(struct pt_regs *ctx) {
    struct connect_info_t *info = connect_info.lookup(&pid_tgid);
    struct sock *sk = info->sk;  // 取出socket
    get_sock_info(sk, ...);      // 读取地址
}
```

### BPF Maps (数据结构)

eBPF程序使用特殊的map存储数据：

```c
// Hash map: 键值对存储
BPF_HASH(connect_info, u64, struct connect_info_t);

// 操作:
connect_info.update(&key, &value);  // 插入/更新
connect_info.lookup(&key);           // 查找
connect_info.delete(&key);           // 删除

// Perf buffer: 向用户空间发送事件流
BPF_PERF_OUTPUT(events);

// 操作:
events.perf_submit(ctx, &data, size);  // 发送事件
```

---

## 关键技术点

### 1. 为什么需要root权限？

eBPF程序加载到内核需要`CAP_BPF`或`CAP_SYS_ADMIN`能力：
```bash
sudo python3 app.py  # 需要sudo
```

### 2. 如何调试eBPF程序？

```python
# 在Python中打印BPF map内容
print(list(self.bpf['connect_info'].items()))

# 查看eBPF程序加载状态
sudo bpftool prog list
sudo bpftool map list
```

### 3. 性能开销

- CPU采样频率: 10Hz (每秒10次)
- 网络事件: 每个TCP操作触发一次
- 开销: 通常 < 5% CPU

### 4. 为什么地址显示0.0.0.0？

**原因**: socket状态未就绪
- TCP连接建立是异步的
- connect()返回时地址可能还是0
- 需要在数据传输时 (sendmsg/recvmsg) 再次读取

**修复**: 刚才的修改确保在返回探针中正确读取socket地址

---

## 总结

这个eBPF调试器是一个**三层架构**:

1. **展示层** (前端): HTML + Chart.js + WebSocket
2. **业务层** (Flask): 数据聚合 + API服务 + WebSocket推送
3. **数据层** (eBPF): 内核事件捕获 + 数据采集

**核心优势**:
- 无需修改网盘源码即可监控
- 实时可视化系统行为
- 极低的性能开销
- 安全的内核级观测

**技术栈**:
- eBPF + BCC (内核监控)
- Flask + SocketIO (Web服务)
- Chart.js (数据可视化)
- Python + C (混合编程)
