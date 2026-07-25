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
					      __u64 timestamp_ns)
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

	event = bpf_ringbuf_reserve(&event, sizeof(*event),
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

	// bpf_ringbuf_discard(event, 0);//暂时放弃刚申请的事件，不发送给
	// loader；reserve 成功后必须调用 submit 或
	// discard；当前还没有填充事件字段，所以先 discard；
	bpf_get_current_comm(
	    event->network.comm,
	    sizeof(event->network)); // 调用 BPF helper，读取当前进程的名称。
	read_sock_info(sk, &event->network.saddr, &event->network.daddr,
		       &event->network.sport, &event->network.dport);

	bpf_ringbuf_submit(event, 0);

	return 0;
}
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
int BPF_KRETPROBE(handle_tcp_v4_connect_ret,int ret){
	struct connect_info *info;
	__u64 pid_tgid;

	pid_tgid=bpf_get_current_pid_tgid();

	info=bpf_map_lookup_elem(&connect_info_map,&pid_tgid);

	if (!info) {
		return 0;
	}





	
	bpf_map_delete_elem(&connect_info_map,&pid_tgid);

	return 0;
}