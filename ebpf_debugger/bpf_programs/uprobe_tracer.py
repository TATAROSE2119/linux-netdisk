"""
Uprobe Tracer - eBPF program for tracing user-space functions

This module uses BCC to trace:
- Server functions (handle_client, send_directory_tree, etc.)
- Client functions (send_file, receive_file, etc.)
"""

from bcc import BPF
import os

# eBPF程序源码
BPF_PROGRAM = """
#include <uapi/linux/ptrace.h>

// 事件数据结构
struct func_event_t {
    u32 pid;
    u32 tid;
    char comm[16];
    char func[32];
    u64 timestamp;
    u64 duration_ns;
    u8 is_return;
    long ret_value;
};

// Perf buffer
BPF_PERF_OUTPUT(func_events);

// Hash to store function entry timestamps
BPF_HASH(func_start, u64, u64);

// 通用函数入口处理
static int trace_func_entry(struct pt_regs *ctx, const char *func_name) {
    u64 pid_tgid = bpf_get_current_pid_tgid();
    u64 ts = bpf_ktime_get_ns();
    func_start.update(&pid_tgid, &ts);

    struct func_event_t event = {};
    event.pid = pid_tgid >> 32;
    event.tid = pid_tgid;
    event.timestamp = ts;
    event.is_return = 0;
    bpf_get_current_comm(&event.comm, sizeof(event.comm));

    return 0;
}

// handle_client入口
int trace_handle_client_entry(struct pt_regs *ctx) {
    u64 pid_tgid = bpf_get_current_pid_tgid();
    u64 ts = bpf_ktime_get_ns();
    func_start.update(&pid_tgid, &ts);

    struct func_event_t event = {};
    event.pid = pid_tgid >> 32;
    event.tid = pid_tgid;
    event.timestamp = ts;
    event.is_return = 0;
    __builtin_memcpy(event.func, "handle_client", 14);
    bpf_get_current_comm(&event.comm, sizeof(event.comm));

    func_events.perf_submit(ctx, &event, sizeof(event));
    return 0;
}

// handle_client返回
int trace_handle_client_return(struct pt_regs *ctx) {
    u64 pid_tgid = bpf_get_current_pid_tgid();
    u64 *start_ts = func_start.lookup(&pid_tgid);
    if (!start_ts) return 0;

    struct func_event_t event = {};
    event.pid = pid_tgid >> 32;
    event.tid = pid_tgid;
    event.timestamp = bpf_ktime_get_ns();
    event.duration_ns = event.timestamp - *start_ts;
    event.is_return = 1;
    event.ret_value = PT_REGS_RC(ctx);
    __builtin_memcpy(event.func, "handle_client", 14);
    bpf_get_current_comm(&event.comm, sizeof(event.comm));

    func_events.perf_submit(ctx, &event, sizeof(event));
    func_start.delete(&pid_tgid);
    return 0;
}

// send_directory_tree入口
int trace_send_dir_tree_entry(struct pt_regs *ctx) {
    u64 pid_tgid = bpf_get_current_pid_tgid();
    u64 ts = bpf_ktime_get_ns();
    func_start.update(&pid_tgid, &ts);

    struct func_event_t event = {};
    event.pid = pid_tgid >> 32;
    event.tid = pid_tgid;
    event.timestamp = ts;
    event.is_return = 0;
    __builtin_memcpy(event.func, "send_directory_tree", 20);
    bpf_get_current_comm(&event.comm, sizeof(event.comm));

    func_events.perf_submit(ctx, &event, sizeof(event));
    return 0;
}

// send_directory_tree返回
int trace_send_dir_tree_return(struct pt_regs *ctx) {
    u64 pid_tgid = bpf_get_current_pid_tgid();
    u64 *start_ts = func_start.lookup(&pid_tgid);
    if (!start_ts) return 0;

    struct func_event_t event = {};
    event.pid = pid_tgid >> 32;
    event.tid = pid_tgid;
    event.timestamp = bpf_ktime_get_ns();
    event.duration_ns = event.timestamp - *start_ts;
    event.is_return = 1;
    __builtin_memcpy(event.func, "send_directory_tree", 20);
    bpf_get_current_comm(&event.comm, sizeof(event.comm));

    func_events.perf_submit(ctx, &event, sizeof(event));
    func_start.delete(&pid_tgid);
    return 0;
}

// sha256_string入口
int trace_sha256_entry(struct pt_regs *ctx) {
    u64 pid_tgid = bpf_get_current_pid_tgid();
    u64 ts = bpf_ktime_get_ns();
    func_start.update(&pid_tgid, &ts);

    struct func_event_t event = {};
    event.pid = pid_tgid >> 32;
    event.tid = pid_tgid;
    event.timestamp = ts;
    event.is_return = 0;
    __builtin_memcpy(event.func, "sha256_string", 14);
    bpf_get_current_comm(&event.comm, sizeof(event.comm));

    func_events.perf_submit(ctx, &event, sizeof(event));
    return 0;
}

// sha256_string返回
int trace_sha256_return(struct pt_regs *ctx) {
    u64 pid_tgid = bpf_get_current_pid_tgid();
    u64 *start_ts = func_start.lookup(&pid_tgid);
    if (!start_ts) return 0;

    struct func_event_t event = {};
    event.pid = pid_tgid >> 32;
    event.tid = pid_tgid;
    event.timestamp = bpf_ktime_get_ns();
    event.duration_ns = event.timestamp - *start_ts;
    event.is_return = 1;
    __builtin_memcpy(event.func, "sha256_string", 14);
    bpf_get_current_comm(&event.comm, sizeof(event.comm));

    func_events.perf_submit(ctx, &event, sizeof(event));
    func_start.delete(&pid_tgid);
    return 0;
}
"""


class UprobeTracer:
    """用户态函数追踪器"""

    # 需要追踪的函数列表
    SERVER_FUNCTIONS = [
        ('handle_client', 'trace_handle_client_entry', 'trace_handle_client_return'),
        ('send_directory_tree', 'trace_send_dir_tree_entry', 'trace_send_dir_tree_return'),
        ('sha256_string', 'trace_sha256_entry', 'trace_sha256_return'),
    ]

    def __init__(self, collector, server_path=None, client_path=None):
        """
        初始化uprobe追踪器

        Args:
            collector: PerfCollector实例
            server_path: 服务器可执行文件路径
            client_path: 客户端可执行文件路径
        """
        self.collector = collector
        self.server_path = server_path
        self.client_path = client_path
        self.bpf = None
        self.running = False
        self.attached_probes = []

    def start(self):
        """启动追踪"""
        # 加载BPF程序
        self.bpf = BPF(text=BPF_PROGRAM)

        # 附加server的uprobe
        if self.server_path and os.path.exists(self.server_path):
            for func_name, entry_fn, return_fn in self.SERVER_FUNCTIONS:
                try:
                    self.bpf.attach_uprobe(
                        name=self.server_path,
                        sym=func_name,
                        fn_name=entry_fn
                    )
                    self.bpf.attach_uretprobe(
                        name=self.server_path,
                        sym=func_name,
                        fn_name=return_fn
                    )
                    self.attached_probes.append(func_name)
                    print(f"[UprobeTracer] Attached to {func_name}")
                except Exception as e:
                    print(f"[UprobeTracer] Failed to attach {func_name}: {e}")

        # 设置事件回调
        self.bpf["func_events"].open_perf_buffer(self._handle_event)
        self.running = True

        print(f"[UprobeTracer] Started, attached {len(self.attached_probes)} probes")

    def poll(self, timeout=100):
        """轮询事件"""
        if self.bpf and self.running:
            self.bpf.perf_buffer_poll(timeout=timeout)

    def stop(self):
        """停止追踪"""
        self.running = False
        if self.bpf:
            self.bpf.cleanup()
            self.bpf = None
        self.attached_probes = []
        print("[UprobeTracer] Stopped")

    def _handle_event(self, cpu, data, size):
        """处理eBPF事件"""
        event = self.bpf["func_events"].event(data)

        pid = event.pid
        comm = event.comm.decode('utf-8', errors='replace')
        func_name = event.func.decode('utf-8', errors='replace').rstrip('\x00')
        timestamp = event.timestamp
        is_return = event.is_return
        duration_ns = event.duration_ns
        ret_value = event.ret_value

        if is_return:
            self.collector.on_function_return(
                pid=pid,
                comm=comm,
                func_name=func_name,
                duration_ns=duration_ns,
                ret_value=ret_value
            )
        else:
            self.collector.on_function_entry(
                pid=pid,
                comm=comm,
                func_name=func_name,
                timestamp_ns=timestamp
            )


# 测试代码
if __name__ == "__main__":
    import sys
    sys.path.insert(0, '..')
    from collectors.perf import PerfCollector

    # 获取server路径
    script_dir = os.path.dirname(os.path.abspath(__file__))
    server_path = os.path.join(script_dir, '..', '..', 'server', 'server')

    collector = PerfCollector()
    tracer = UprobeTracer(collector, server_path=server_path)

    try:
        tracer.start()
        print("Tracing user functions... Press Ctrl+C to stop")
        while True:
            tracer.poll()
    except KeyboardInterrupt:
        pass
    finally:
        tracer.stop()
        print("\nStats:", collector.get_stats())
