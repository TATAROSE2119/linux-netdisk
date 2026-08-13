#include "vmlinux.h"

#include <bpf/bpf_core_read.h>
#include <bpf/bpf_endian.h>
#include <bpf/bpf_tracing.h>

#include <bpf/bpf_helpers.h>
#include "include/events.h"

#define NETDISK_AF_INET 2
#define NETDISK_EINPROGRESS 115
/*
 * 0 表示监控所有进程。
 * 任务 3 再通过 loader 的 skeleton rodata 设置这个值。
 */
const volatile __u32 target_pid = 0;
struct connect_info {
	__u64 timestamp_ns;
	struct sock *sk;
};

struct io_info {
	struct sock *sk;
	__u64 requested;
};

struct syscall_start_info {
	__u64 timestamp_ns;
	__u8 syscall_id;
	__u8 reserved[7];
};

struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, 1 << 24);
} events SEC(".maps");

char LICENSE[] SEC("license") = "Dual BSD/GPL";

struct {
	__uint(type, BPF_MAP_TYPE_HASH); // 指定 map 类型为哈希表。
	__uint(max_entries, 10240);
	__type(key, __u64);
	__type(value, struct connect_info);
} connect_info_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 10240);
	__type(key, __u64);
	__type(value, struct io_info);
} send_info_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 10240);
	__type(key, __u64);
	__type(value, struct io_info);
} recv_info_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 10240);
	__type(key, __u64);
	__type(value, struct syscall_start_info);
} syscall_start_map SEC(".maps");

// 实现 PID 过滤函数。它决定当前事件是否应该继续处理。
static __always_inline int should_trace(__u32 pid)
{
	return target_pid == 0 || target_pid == pid;
}
static __always_inline void read_sock_info(struct sock *sk,
					   __u32 *saddr, // 源 IPv4 地址
					   __u32 *daddr, // 目标 IPv4 地址
					   __u16 *sport, // 源端口
					   __u16 *dport	 // 目标端口
)
{
	__u16 dport_be; // 保存未经转换的目标端口。
	*saddr = BPF_CORE_READ(sk, __sk_common.skc_rcv_saddr);
	*daddr = BPF_CORE_READ(sk, __sk_common.skc_daddr);
	*sport = BPF_CORE_READ(sk, __sk_common.skc_num);

	dport_be = BPF_CORE_READ(sk, __sk_common.skc_dport);
	*dport = bpf_ntohs(dport_be);
}
// 检查 socket、PID 和地址族。
static __always_inline int emit_network_event(struct sock *sk, __u8 action,
					      __u64 timestamp_ns, __u64 bytes)
{
	struct netdisk_event *event; // 将指向 ring buffer
				     // 为本次事件预留的内存。
	__u64 pid_tgid;
	__u32 pid;
	__u16 family;

	if (!sk) {
		return 0;
	}
	pid_tgid = bpf_get_current_pid_tgid();
	pid = pid_tgid >> 32;

	if (!should_trace(pid)) {
		return 0;
	}
	family = BPF_CORE_READ(sk, __sk_common.skc_family);
	if (family != NETDISK_AF_INET) {
		return 0;
	}

	event = bpf_ringbuf_reserve(&events, sizeof(*event),
				    0); // 这行向 ring buffer 申请一块内存：
	if (!event) {
		return 0;
	}

	__builtin_memset(
	    event, 0, sizeof(*event)); // 将整块事件内存初始化为
				       // 0；使用编译器内建的 memset，Clang
				       // 会把它展开成 verifier 能分析的写操作。

	event->kind = NETDISK_EVENT_NETWORK;  // 指定网络事件类型
	event->network.pid = pid;	      // 拿到进程PID
	event->network.tid = (__u32)pid_tgid; // 拿到现场TID
	event->network.action = action; // 写入调用者传来的动作类型。
	event->network.timestamp_ns = timestamp_ns;
	event->network.bytes = bytes;

	// bpf_ringbuf_discard(event, 0);//暂时放弃刚申请的事件，不发送给
	// loader；reserve 成功后必须调用 submit 或
	// discard；当前还没有填充事件字段，所以先 discard；
	bpf_get_current_comm(
	    event->network.comm,
	    sizeof(
		event->network.comm)); // 调用 BPF helper，读取当前进程的名称。
	read_sock_info(sk, &event->network.saddr, &event->network.daddr,
		       &event->network.sport, &event->network.dport);

	bpf_ringbuf_submit(event, 0);

	return 0;
}

static __always_inline int trace_syscall_entry(__u8 syscall_id)
{
	struct syscall_start_info info = {};
	__u64 pid_tgid;
	__u32 pid;

	pid_tgid = bpf_get_current_pid_tgid();
	pid = pid_tgid >> 32;
	if (!should_trace(pid))
		return 0;

	info.timestamp_ns = bpf_ktime_get_ns();
	info.syscall_id = syscall_id;
	bpf_map_update_elem(&syscall_start_map, &pid_tgid, &info, BPF_ANY);
	return 0;
}

static __always_inline int
trace_syscall_exit(struct trace_event_raw_sys_exit *ctx, __u8 syscall_id)
{
	struct syscall_start_info *info;
	struct netdisk_event *event;
	__u64 pid_tgid;
	__u64 start_ns;
	__u64 timestamp_ns;
	__u8 stored_syscall_id;

	pid_tgid = bpf_get_current_pid_tgid();
	info = bpf_map_lookup_elem(&syscall_start_map, &pid_tgid);
	if (!info)
		return 0;

	start_ns = info->timestamp_ns;
	stored_syscall_id = info->syscall_id;
	bpf_map_delete_elem(&syscall_start_map, &pid_tgid);

	if (stored_syscall_id != syscall_id)
		return 0;

	timestamp_ns = bpf_ktime_get_ns();
	event = bpf_ringbuf_reserve(&events, sizeof(*event), 0);
	if (!event)
		return 0;

	__builtin_memset(event, 0, sizeof(*event));
	event->kind = NETDISK_EVENT_SYSCALL;
	event->syscall.pid = pid_tgid >> 32;
	event->syscall.tid = (__u32)pid_tgid;
	event->syscall.timestamp_ns = timestamp_ns;
	event->syscall.duration_ns = timestamp_ns - start_ns;
	event->syscall.ret = ctx->ret;
	event->syscall.syscall_id = syscall_id;
	bpf_get_current_comm(event->syscall.comm,
			     sizeof(event->syscall.comm));
	bpf_ringbuf_submit(event, 0);
	return 0;
}

#define DEFINE_SYSCALL_PROBES(syscall_name, syscall_id_value)                 \
	SEC("tracepoint/syscalls/sys_enter_" #syscall_name)                    \
	int handle_sys_enter_##syscall_name(                                    \
		struct trace_event_raw_sys_enter *ctx)                            \
	{                                                                       \
		(void)ctx;                                                        \
		return trace_syscall_entry(syscall_id_value);                     \
	}                                                                       \
	SEC("tracepoint/syscalls/sys_exit_" #syscall_name)                     \
	int handle_sys_exit_##syscall_name(                                     \
		struct trace_event_raw_sys_exit *ctx)                             \
	{                                                                       \
		return trace_syscall_exit(ctx, syscall_id_value);                 \
	}

DEFINE_SYSCALL_PROBES(read, NETDISK_SYSCALL_READ)
DEFINE_SYSCALL_PROBES(write, NETDISK_SYSCALL_WRITE)
DEFINE_SYSCALL_PROBES(openat, NETDISK_SYSCALL_OPENAT)
DEFINE_SYSCALL_PROBES(close, NETDISK_SYSCALL_CLOSE)
DEFINE_SYSCALL_PROBES(accept4, NETDISK_SYSCALL_ACCEPT4)
DEFINE_SYSCALL_PROBES(sendto, NETDISK_SYSCALL_SENDTO)
DEFINE_SYSCALL_PROBES(recvfrom, NETDISK_SYSCALL_RECVFROM)

// 实现主动连接入口 probe。先只建立 probe、读取 PID 并执行过滤，不写 map。
SEC("kprobe/tcp_v4_connect")
int BPF_KPROBE(handle_tcp_v4_connect, struct sock *sk)
{
	__u64 pid_tgid;
	__u32 pid;
	struct connect_info info = {};

	pid_tgid = bpf_get_current_pid_tgid();
	pid = pid_tgid >> 32;

	if (!should_trace(pid)) {
		return 0;
	}

	info.timestamp_ns = bpf_ktime_get_ns();
	info.sk = sk;

	bpf_map_update_elem(&connect_info_map, &pid_tgid, &info, BPF_ANY);

	return 0;
}
SEC("kretprobe/tcp_v4_connect")
int BPF_KRETPROBE(handle_tcp_v4_connect_ret, int ret)
{
	struct connect_info *info;
	struct sock *sk;
	__u64 pid_tgid;
	__u64 timestamp_ns;

	pid_tgid = bpf_get_current_pid_tgid();

	info = bpf_map_lookup_elem(&connect_info_map, &pid_tgid);

	if (!info) {
		return 0;
	}

	sk = info->sk;
	timestamp_ns = info->timestamp_ns;
	bpf_map_delete_elem(&connect_info_map, &pid_tgid);

	if (ret != 0 && ret != -NETDISK_EINPROGRESS) {
		return 0;
	}

	return emit_network_event(sk, NETDISK_NET_CONNECT, timestamp_ns, 0);
}
SEC("kretprobe/inet_csk_accept")
int BPF_KRETPROBE(handle_inet_csk_accept_ret, struct sock *sk)
{
	if (!sk) {
		return 0;
	}

	return emit_network_event(sk, NETDISK_NET_ACCEPT, bpf_ktime_get_ns(), 0);
}
SEC("kprobe/tcp_close")
int BPF_KPROBE(handle_tcp_close, struct sock *sk)
{
	return emit_network_event(sk, NETDISK_NET_CLOSE, bpf_ktime_get_ns(), 0);
}

SEC("kprobe/tcp_sendmsg")
int BPF_KPROBE(handle_tcp_sendmsg, struct sock *sk, struct msghdr *msg,
	       size_t size)
{
	struct io_info info = {};
	__u64 pid_tgid;
	__u32 pid;

	(void)msg;
	if (!sk)
		return 0;

	pid_tgid = bpf_get_current_pid_tgid();
	pid = pid_tgid >> 32;
	if (!should_trace(pid))
		return 0;

	info.sk = sk;
	info.requested = size;
	bpf_map_update_elem(&send_info_map, &pid_tgid, &info, BPF_ANY);
	return 0;
}

SEC("kretprobe/tcp_sendmsg")
int BPF_KRETPROBE(handle_tcp_sendmsg_ret, int ret)
{
	struct io_info *info;
	struct sock *sk;
	__u64 pid_tgid;

	pid_tgid = bpf_get_current_pid_tgid();
	info = bpf_map_lookup_elem(&send_info_map, &pid_tgid);
	if (!info)
		return 0;

	sk = info->sk;
	bpf_map_delete_elem(&send_info_map, &pid_tgid);
	if (ret <= 0)
		return 0;

	return emit_network_event(sk, NETDISK_NET_SEND, bpf_ktime_get_ns(),
				  (__u64)ret);
}

SEC("kprobe/tcp_recvmsg")
int BPF_KPROBE(handle_tcp_recvmsg, struct sock *sk, struct msghdr *msg,
	       size_t len)
{
	struct io_info info = {};
	__u64 pid_tgid;
	__u32 pid;

	(void)msg;
	if (!sk)
		return 0;

	pid_tgid = bpf_get_current_pid_tgid();
	pid = pid_tgid >> 32;
	if (!should_trace(pid))
		return 0;

	info.sk = sk;
	info.requested = len;
	bpf_map_update_elem(&recv_info_map, &pid_tgid, &info, BPF_ANY);
	return 0;
}

SEC("kretprobe/tcp_recvmsg")
int BPF_KRETPROBE(handle_tcp_recvmsg_ret, int ret)
{
	struct io_info *info;
	struct sock *sk;
	__u64 pid_tgid;

	pid_tgid = bpf_get_current_pid_tgid();
	info = bpf_map_lookup_elem(&recv_info_map, &pid_tgid);
	if (!info)
		return 0;

	sk = info->sk;
	bpf_map_delete_elem(&recv_info_map, &pid_tgid);
	if (ret <= 0)
		return 0;

	return emit_network_event(sk, NETDISK_NET_RECV, bpf_ktime_get_ns(),
				  (__u64)ret);
}
