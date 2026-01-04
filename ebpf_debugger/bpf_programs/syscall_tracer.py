"""
Syscall Tracer - eBPF program for tracing system calls

This module uses BCC to trace:
- File operations (open, read, write, close)
- Socket operations (socket, connect, accept, send, recv)
- Process operations
"""

from bcc import BPF
import os

# eBPF程序源码
BPF_PROGRAM = """
#include <uapi/linux/ptrace.h>
#include <linux/sched.h>
#include <linux/fs.h>

// 事件数据结构
struct syscall_event_t {
    u32 pid;
    u32 tid;
    char comm[16];
    char syscall[16];
    u64 start_ts;
    u64 duration_ns;
    long ret;
    u64 arg0;
    u64 arg1;
    u64 arg2;
};

// Perf buffer
BPF_PERF_OUTPUT(syscall_events);

// Hash to store syscall entry timestamps
BPF_HASH(syscall_start, u64, u64);

// 通用系统调用入口追踪
static int trace_syscall_entry(void *ctx, const char *name) {
    u64 pid_tgid = bpf_get_current_pid_tgid();
    u64 ts = bpf_ktime_get_ns();
    syscall_start.update(&pid_tgid, &ts);
    return 0;
}

// 文件读取入口
TRACEPOINT_PROBE(syscalls, sys_enter_read) {
    u64 pid_tgid = bpf_get_current_pid_tgid();
    u64 ts = bpf_ktime_get_ns();
    syscall_start.update(&pid_tgid, &ts);
    return 0;
}

// 文件读取返回
TRACEPOINT_PROBE(syscalls, sys_exit_read) {
    u64 pid_tgid = bpf_get_current_pid_tgid();
    u32 pid = pid_tgid >> 32;

    FILTER_PID

    u64 *start_ts = syscall_start.lookup(&pid_tgid);
    if (!start_ts) return 0;

    struct syscall_event_t event = {};
    event.pid = pid;
    event.tid = pid_tgid;
    __builtin_memcpy(event.syscall, "read", 5);
    event.start_ts = *start_ts;
    event.duration_ns = bpf_ktime_get_ns() - *start_ts;
    event.ret = args->ret;
    bpf_get_current_comm(&event.comm, sizeof(event.comm));

    syscall_events.perf_submit(args, &event, sizeof(event));
    syscall_start.delete(&pid_tgid);
    return 0;
}

// 文件写入入口
TRACEPOINT_PROBE(syscalls, sys_enter_write) {
    u64 pid_tgid = bpf_get_current_pid_tgid();
    u64 ts = bpf_ktime_get_ns();
    syscall_start.update(&pid_tgid, &ts);
    return 0;
}

// 文件写入返回
TRACEPOINT_PROBE(syscalls, sys_exit_write) {
    u64 pid_tgid = bpf_get_current_pid_tgid();
    u32 pid = pid_tgid >> 32;

    FILTER_PID

    u64 *start_ts = syscall_start.lookup(&pid_tgid);
    if (!start_ts) return 0;

    struct syscall_event_t event = {};
    event.pid = pid;
    event.tid = pid_tgid;
    __builtin_memcpy(event.syscall, "write", 6);
    event.start_ts = *start_ts;
    event.duration_ns = bpf_ktime_get_ns() - *start_ts;
    event.ret = args->ret;
    bpf_get_current_comm(&event.comm, sizeof(event.comm));

    syscall_events.perf_submit(args, &event, sizeof(event));
    syscall_start.delete(&pid_tgid);
    return 0;
}

// openat入口
TRACEPOINT_PROBE(syscalls, sys_enter_openat) {
    u64 pid_tgid = bpf_get_current_pid_tgid();
    u64 ts = bpf_ktime_get_ns();
    syscall_start.update(&pid_tgid, &ts);
    return 0;
}

// openat返回
TRACEPOINT_PROBE(syscalls, sys_exit_openat) {
    u64 pid_tgid = bpf_get_current_pid_tgid();
    u32 pid = pid_tgid >> 32;

    FILTER_PID

    u64 *start_ts = syscall_start.lookup(&pid_tgid);
    if (!start_ts) return 0;

    struct syscall_event_t event = {};
    event.pid = pid;
    event.tid = pid_tgid;
    __builtin_memcpy(event.syscall, "openat", 7);
    event.start_ts = *start_ts;
    event.duration_ns = bpf_ktime_get_ns() - *start_ts;
    event.ret = args->ret;
    bpf_get_current_comm(&event.comm, sizeof(event.comm));

    syscall_events.perf_submit(args, &event, sizeof(event));
    syscall_start.delete(&pid_tgid);
    return 0;
}

// close入口
TRACEPOINT_PROBE(syscalls, sys_enter_close) {
    u64 pid_tgid = bpf_get_current_pid_tgid();
    u64 ts = bpf_ktime_get_ns();
    syscall_start.update(&pid_tgid, &ts);
    return 0;
}

// close返回
TRACEPOINT_PROBE(syscalls, sys_exit_close) {
    u64 pid_tgid = bpf_get_current_pid_tgid();
    u32 pid = pid_tgid >> 32;

    FILTER_PID

    u64 *start_ts = syscall_start.lookup(&pid_tgid);
    if (!start_ts) return 0;

    struct syscall_event_t event = {};
    event.pid = pid;
    event.tid = pid_tgid;
    __builtin_memcpy(event.syscall, "close", 6);
    event.start_ts = *start_ts;
    event.duration_ns = bpf_ktime_get_ns() - *start_ts;
    event.ret = args->ret;
    bpf_get_current_comm(&event.comm, sizeof(event.comm));

    syscall_events.perf_submit(args, &event, sizeof(event));
    syscall_start.delete(&pid_tgid);
    return 0;
}

// accept4入口
TRACEPOINT_PROBE(syscalls, sys_enter_accept4) {
    u64 pid_tgid = bpf_get_current_pid_tgid();
    u64 ts = bpf_ktime_get_ns();
    syscall_start.update(&pid_tgid, &ts);
    return 0;
}

// accept4返回
TRACEPOINT_PROBE(syscalls, sys_exit_accept4) {
    u64 pid_tgid = bpf_get_current_pid_tgid();
    u32 pid = pid_tgid >> 32;

    FILTER_PID

    u64 *start_ts = syscall_start.lookup(&pid_tgid);
    if (!start_ts) return 0;

    struct syscall_event_t event = {};
    event.pid = pid;
    event.tid = pid_tgid;
    __builtin_memcpy(event.syscall, "accept4", 8);
    event.start_ts = *start_ts;
    event.duration_ns = bpf_ktime_get_ns() - *start_ts;
    event.ret = args->ret;
    bpf_get_current_comm(&event.comm, sizeof(event.comm));

    syscall_events.perf_submit(args, &event, sizeof(event));
    syscall_start.delete(&pid_tgid);
    return 0;
}

// sendto入口
TRACEPOINT_PROBE(syscalls, sys_enter_sendto) {
    u64 pid_tgid = bpf_get_current_pid_tgid();
    u64 ts = bpf_ktime_get_ns();
    syscall_start.update(&pid_tgid, &ts);
    return 0;
}

// sendto返回
TRACEPOINT_PROBE(syscalls, sys_exit_sendto) {
    u64 pid_tgid = bpf_get_current_pid_tgid();
    u32 pid = pid_tgid >> 32;

    FILTER_PID

    u64 *start_ts = syscall_start.lookup(&pid_tgid);
    if (!start_ts) return 0;

    struct syscall_event_t event = {};
    event.pid = pid;
    event.tid = pid_tgid;
    __builtin_memcpy(event.syscall, "sendto", 7);
    event.start_ts = *start_ts;
    event.duration_ns = bpf_ktime_get_ns() - *start_ts;
    event.ret = args->ret;
    bpf_get_current_comm(&event.comm, sizeof(event.comm));

    syscall_events.perf_submit(args, &event, sizeof(event));
    syscall_start.delete(&pid_tgid);
    return 0;
}

// recvfrom入口
TRACEPOINT_PROBE(syscalls, sys_enter_recvfrom) {
    u64 pid_tgid = bpf_get_current_pid_tgid();
    u64 ts = bpf_ktime_get_ns();
    syscall_start.update(&pid_tgid, &ts);
    return 0;
}

// recvfrom返回
TRACEPOINT_PROBE(syscalls, sys_exit_recvfrom) {
    u64 pid_tgid = bpf_get_current_pid_tgid();
    u32 pid = pid_tgid >> 32;

    FILTER_PID

    u64 *start_ts = syscall_start.lookup(&pid_tgid);
    if (!start_ts) return 0;

    struct syscall_event_t event = {};
    event.pid = pid;
    event.tid = pid_tgid;
    __builtin_memcpy(event.syscall, "recvfrom", 9);
    event.start_ts = *start_ts;
    event.duration_ns = bpf_ktime_get_ns() - *start_ts;
    event.ret = args->ret;
    bpf_get_current_comm(&event.comm, sizeof(event.comm));

    syscall_events.perf_submit(args, &event, sizeof(event));
    syscall_start.delete(&pid_tgid);
    return 0;
}
"""


class SyscallTracer:
    """系统调用追踪器"""

    def __init__(self, collector, pid_filter=None, comm_filter=None):
        """
        初始化系统调用追踪器

        Args:
            collector: SyscallCollector实例
            pid_filter: 只追踪指定PID(可选)
            comm_filter: 只追踪指定进程名(可选)
        """
        self.collector = collector
        self.pid_filter = pid_filter
        self.comm_filter = comm_filter
        self.bpf = None
        self.running = False

    def start(self):
        """启动追踪"""
        program = BPF_PROGRAM

        # 添加PID过滤
        if self.pid_filter:
            program = program.replace(
                'FILTER_PID',
                f'if (pid != {self.pid_filter}) return 0;'
            )
        else:
            program = program.replace('FILTER_PID', '')

        # 加载BPF程序
        self.bpf = BPF(text=program)

        # 设置事件回调
        self.bpf["syscall_events"].open_perf_buffer(self._handle_event)
        self.running = True

        print("[SyscallTracer] Started")

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
        print("[SyscallTracer] Stopped")

    def _handle_event(self, cpu, data, size):
        """处理eBPF事件"""
        event = self.bpf["syscall_events"].event(data)

        pid = event.pid
        comm = event.comm.decode('utf-8', errors='replace')
        syscall = event.syscall.decode('utf-8', errors='replace').rstrip('\x00')
        duration_ns = event.duration_ns
        ret = event.ret

        # 进程名过滤
        if self.comm_filter and self.comm_filter not in comm:
            return

        self.collector.on_syscall(
            pid=pid,
            comm=comm,
            syscall=syscall,
            duration_ns=duration_ns,
            ret=ret
        )


# 测试代码
if __name__ == "__main__":
    import sys
    sys.path.insert(0, '..')
    from collectors.syscall import SyscallCollector

    collector = SyscallCollector()
    tracer = SyscallTracer(collector)

    try:
        tracer.start()
        print("Tracing syscalls... Press Ctrl+C to stop")
        while True:
            tracer.poll()
    except KeyboardInterrupt:
        pass
    finally:
        tracer.stop()
        print("\nStats:", collector.get_stats())
