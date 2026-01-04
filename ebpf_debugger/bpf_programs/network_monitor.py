"""
Network Monitor - eBPF program for monitoring TCP connections and traffic

This module uses BCC to trace:
- TCP connections (connect/accept/close)
- Data transfer (send/recv)
- Network latency
"""

from bcc import BPF
import socket
import struct

# eBPF程序源码
BPF_PROGRAM = """
#include <uapi/linux/ptrace.h>
#include <net/sock.h>
#include <bcc/proto.h>
#include <linux/tcp.h>

// 事件类型
#define EVENT_CONNECT 1
#define EVENT_ACCEPT  2
#define EVENT_CLOSE   3
#define EVENT_SEND    4
#define EVENT_RECV    5

// 事件数据结构
struct event_t {
    u32 pid;
    u32 tid;
    char comm[16];
    u8 event_type;
    u32 saddr;
    u32 daddr;
    u16 sport;
    u16 dport;
    u64 bytes;
    u64 timestamp;
};

// Perf buffer for events
BPF_PERF_OUTPUT(events);

// 用于跟踪connect调用的时间戳和socket
struct connect_info_t {
    u64 ts;
    struct sock *sk;
};
BPF_HASH(connect_info, u64, struct connect_info_t);

// Helper to get socket addresses
static int get_sock_info(struct sock *sk, u32 *saddr, u32 *daddr, u16 *sport, u16 *dport) {
    bpf_probe_read(saddr, sizeof(*saddr), &sk->__sk_common.skc_rcv_saddr);
    bpf_probe_read(daddr, sizeof(*daddr), &sk->__sk_common.skc_daddr);
    bpf_probe_read(sport, sizeof(*sport), &sk->__sk_common.skc_num);
    u16 dport_be;
    bpf_probe_read(&dport_be, sizeof(dport_be), &sk->__sk_common.skc_dport);
    *dport = ntohs(dport_be);
    return 0;
}

// Trace tcp_v4_connect (outgoing connections)
int trace_tcp_connect(struct pt_regs *ctx, struct sock *sk) {
    u64 pid_tgid = bpf_get_current_pid_tgid();
    u32 pid = pid_tgid >> 32;

    // Filter by target ports (optional)
    FILTER_PID

    struct connect_info_t info = {};
    info.ts = bpf_ktime_get_ns();
    info.sk = sk;
    connect_info.update(&pid_tgid, &info);

    return 0;
}

// Trace tcp_v4_connect return
int trace_tcp_connect_ret(struct pt_regs *ctx) {
    u64 pid_tgid = bpf_get_current_pid_tgid();
    u32 pid = pid_tgid >> 32;

    struct connect_info_t *info = connect_info.lookup(&pid_tgid);
    if (!info) return 0;

    int ret = PT_REGS_RC(ctx);
    // 只在连接成功或正在进行时记录
    if (ret != 0 && ret != -115) {  // -115 = EINPROGRESS
        connect_info.delete(&pid_tgid);
        return 0;
    }

    struct event_t event = {};
    event.pid = pid;
    event.tid = pid_tgid;
    event.event_type = EVENT_CONNECT;
    event.timestamp = bpf_ktime_get_ns() - info->ts;
    bpf_get_current_comm(&event.comm, sizeof(event.comm));

    // 获取socket地址信息
    struct sock *sk = info->sk;
    if (sk) {
        get_sock_info(sk, &event.saddr, &event.daddr, &event.sport, &event.dport);
    }

    events.perf_submit(ctx, &event, sizeof(event));
    connect_info.delete(&pid_tgid);

    return 0;
}

// Trace inet_csk_accept return (incoming connections)
int trace_accept_ret(struct pt_regs *ctx) {
    struct sock *sk = (struct sock *)PT_REGS_RC(ctx);
    if (sk == NULL) return 0;

    u64 pid_tgid = bpf_get_current_pid_tgid();
    u32 pid = pid_tgid >> 32;

    struct event_t event = {};
    event.pid = pid;
    event.tid = pid_tgid;
    event.event_type = EVENT_ACCEPT;
    event.timestamp = bpf_ktime_get_ns();
    bpf_get_current_comm(&event.comm, sizeof(event.comm));

    get_sock_info(sk, &event.saddr, &event.daddr, &event.sport, &event.dport);

    events.perf_submit(ctx, &event, sizeof(event));
    return 0;
}

// Trace tcp_close
int trace_tcp_close(struct pt_regs *ctx, struct sock *sk) {
    if (sk == NULL) return 0;

    u64 pid_tgid = bpf_get_current_pid_tgid();
    u32 pid = pid_tgid >> 32;

    struct event_t event = {};
    event.pid = pid;
    event.tid = pid_tgid;
    event.event_type = EVENT_CLOSE;
    event.timestamp = bpf_ktime_get_ns();
    bpf_get_current_comm(&event.comm, sizeof(event.comm));

    get_sock_info(sk, &event.saddr, &event.daddr, &event.sport, &event.dport);

    events.perf_submit(ctx, &event, sizeof(event));
    return 0;
}

// Trace tcp_sendmsg
int trace_tcp_sendmsg(struct pt_regs *ctx, struct sock *sk, struct msghdr *msg, size_t size) {
    if (sk == NULL) return 0;

    u64 pid_tgid = bpf_get_current_pid_tgid();
    u32 pid = pid_tgid >> 32;

    struct event_t event = {};
    event.pid = pid;
    event.tid = pid_tgid;
    event.event_type = EVENT_SEND;
    event.bytes = size;
    event.timestamp = bpf_ktime_get_ns();
    bpf_get_current_comm(&event.comm, sizeof(event.comm));

    get_sock_info(sk, &event.saddr, &event.daddr, &event.sport, &event.dport);

    events.perf_submit(ctx, &event, sizeof(event));
    return 0;
}

// Trace tcp_recvmsg
int trace_tcp_recvmsg(struct pt_regs *ctx, struct sock *sk, struct msghdr *msg, size_t len, int flags, int *addr_len) {
    if (sk == NULL) return 0;

    u64 pid_tgid = bpf_get_current_pid_tgid();
    u32 pid = pid_tgid >> 32;

    struct event_t event = {};
    event.pid = pid;
    event.tid = pid_tgid;
    event.event_type = EVENT_RECV;
    event.bytes = len;
    event.timestamp = bpf_ktime_get_ns();
    bpf_get_current_comm(&event.comm, sizeof(event.comm));

    get_sock_info(sk, &event.saddr, &event.daddr, &event.sport, &event.dport);

    events.perf_submit(ctx, &event, sizeof(event));
    return 0;
}
"""

# 事件类型常量
EVENT_CONNECT = 1
EVENT_ACCEPT = 2
EVENT_CLOSE = 3
EVENT_SEND = 4
EVENT_RECV = 5


def inet_ntoa(addr):
    """Convert integer IP to string"""
    return socket.inet_ntoa(struct.pack("I", addr))


class NetworkMonitor:
    """网络监控器"""

    def __init__(self, collector, pid_filter=None):
        """
        初始化网络监控器

        Args:
            collector: NetworkCollector实例
            pid_filter: 只监控指定PID的进程(可选)
        """
        self.collector = collector
        self.pid_filter = pid_filter
        self.bpf = None
        self.running = False

    def start(self):
        """启动监控"""
        # 准备BPF程序
        program = BPF_PROGRAM
        if self.pid_filter:
            program = program.replace(
                'FILTER_PID',
                f'if (pid != {self.pid_filter}) return 0;'
            )
        else:
            program = program.replace('FILTER_PID', '')

        # 加载BPF程序
        self.bpf = BPF(text=program)

        # 附加kprobes
        self.bpf.attach_kprobe(event="tcp_v4_connect", fn_name="trace_tcp_connect")
        self.bpf.attach_kretprobe(event="tcp_v4_connect", fn_name="trace_tcp_connect_ret")
        self.bpf.attach_kretprobe(event="inet_csk_accept", fn_name="trace_accept_ret")
        self.bpf.attach_kprobe(event="tcp_close", fn_name="trace_tcp_close")
        self.bpf.attach_kprobe(event="tcp_sendmsg", fn_name="trace_tcp_sendmsg")
        self.bpf.attach_kprobe(event="tcp_recvmsg", fn_name="trace_tcp_recvmsg")

        # 设置事件回调
        self.bpf["events"].open_perf_buffer(self._handle_event)
        self.running = True

        print("[NetworkMonitor] Started")

    def poll(self, timeout=100):
        """轮询事件(非阻塞)"""
        if self.bpf and self.running:
            self.bpf.perf_buffer_poll(timeout=timeout)

    def stop(self):
        """停止监控"""
        self.running = False
        if self.bpf:
            self.bpf.cleanup()
            self.bpf = None
        print("[NetworkMonitor] Stopped")

    def _handle_event(self, cpu, data, size):
        """处理eBPF事件"""
        event = self.bpf["events"].event(data)

        pid = event.pid
        comm = event.comm.decode('utf-8', errors='replace')
        saddr = inet_ntoa(event.saddr)
        daddr = inet_ntoa(event.daddr)
        sport = event.sport
        dport = event.dport

        if event.event_type == EVENT_CONNECT:
            self.collector.on_connect(pid, comm, saddr, daddr, sport, dport)

        elif event.event_type == EVENT_ACCEPT:
            self.collector.on_connect(pid, comm, saddr, daddr, sport, dport)

        elif event.event_type == EVENT_CLOSE:
            self.collector.on_close(pid, saddr, daddr, sport, dport)

        elif event.event_type == EVENT_SEND:
            self.collector.on_send(pid, saddr, daddr, sport, dport, event.bytes)

        elif event.event_type == EVENT_RECV:
            self.collector.on_recv(pid, saddr, daddr, sport, dport, event.bytes)


# 测试代码
if __name__ == "__main__":
    import sys
    sys.path.insert(0, '..')
    from collectors.network import NetworkCollector

    collector = NetworkCollector()
    monitor = NetworkMonitor(collector)

    try:
        monitor.start()
        print("Monitoring network... Press Ctrl+C to stop")
        while True:
            monitor.poll()
    except KeyboardInterrupt:
        pass
    finally:
        monitor.stop()
        print("\nStats:", collector.get_stats())
