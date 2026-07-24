#include "vmlinux.h"

#include <bpf/bpf_core_read.h>
#include <bpf/bpf_endian.h>
#include <bpf/bpf_tracing.h>

#include <bpf/bpf_helpers.h>
// #include <sys/cdefs.h>
// #include <linux/bpf.h>
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
//检查 socket、PID 和地址族。
static __always_inline int emit_network_event(
    struct sock *sk,
    __u8 action,
    __u64 timestamp_ns
){
    __u64 pid_tgid;
    __u32 pid;
    __u16 family;

    if (!sk) {
        return 0;
    }
    pid_tgid=bpf_get_current_pid_tgid();
    pid=pid_tgid>>32;

    if (!should_trace(pid)) {
        return 0;
    }
    family=BPF_CORE_READ(sk, __sk_common.skc_family);
    if (family != NETDISK_AF_INET) {
        return 0;
    }

    return 0;
}

