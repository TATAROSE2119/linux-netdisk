/*
 * netdisk_bpf.bpf.c —— 网盘 eBPF 内核态监控程序
 *
 * 本文件是网盘项目的内核态数据面：以内核态 BPF 程序的形式，
 * 采集两类事件并通过 ring buffer 异步上报给用户态 loader
 * （netdisk_loader.c）：
 *
 *   1. 网络事件：挂载 TCP 协议栈的 kprobe / kretprobe，
 *      采集 connect / accept / close / send / recv 五类动作；
 *   2. 系统调用事件：挂载 sys_enter_* / sys_exit_* tracepoint，
 *      采集 read/write/openat/close/accept4/sendto/recvfrom
 *      七类系统调用的耗时与返回值。
 *
 * 整体数据流：
 *
 *   内核态(kprobe/tracepoint) ──起始信息──▶ hash map（供退出时配对）
 *          │
 *          └──▶ ring buffer(events) ──▶ 用户态 ring_buffer__poll()
 *                                          └──▶ handle_event() 打印 JSON
 *
 * 编译方式：由 Makefile 使用 clang -target bpf -g -O2 编译为
 * netdisk_bpf.bpf.o，再由 bpftool gen skeleton 生成
 * netdisk_bpf.skel.h 供用户态 loader 直接使用。
 */

#include "vmlinux.h" /* 内核类型全集（由 BTF 生成），提供 struct sock 等定义 */

#include <bpf/bpf_core_read.h> /* BPF_CORE_READ：CO-RE 方式读取内核结构体字段 */
#include <bpf/bpf_endian.h>    /* bpf_ntohs 等字节序转换 helper */
#include <bpf/bpf_tracing.h>   /* BPF_KPROBE / BPF_KRETPROBE 宏 */

#include <bpf/bpf_helpers.h> /* 基础 helper 声明（bpf_get_current_pid_tgid 等） */
#include "include/events.h"  /* 事件结构体与枚举定义（内核态/用户态共享） */

/* IPv4 地址族编号（AF_INET = 2），用于过滤非 IPv4 连接 */
#define NETDISK_AF_INET 2
/* EINPROGRESS = 115：非阻塞 connect() 返回“连接正在进行”，
 * 对 TCP 属于正常路径，不能当作失败过滤掉 */
#define NETDISK_EINPROGRESS 115
/*
 * 目标进程 PID 过滤变量（位于 skeleton 的 rodata 只读段）。
 *
 * 取值含义：
 *   - 0   ：监控所有进程（默认值，方便整机观察）；
 *   - 非 0：只采集该 PID 进程产生的事件，避免被无关系统流量淹没。
 *
 * 注意：因为 rodata 只读，loader 必须在 BPF 程序 load 之前，
 * 通过 skel->rodata->target_pid = ... 完成赋值（见
 * netdisk_loader.c 的 configure_programs()）；内核加载后无法再修改。
 */
const volatile __u32 target_pid = 0;

/*
 * connect_info：kprobe 与 kretprobe 之间传递的“发起连接”上下文。
 * 入口 probe 记录时间戳与 socket 指针，退出 probe 取出后发出事件。
 */
struct connect_info {
	__u64 timestamp_ns; /* connect 入口时刻（纳秒） */
	struct sock *sk;    /* 对应的内核 socket 对象指针 */
};

/*
 * io_info：send / recv 的入口与退出 probe 之间传递的“IO 请求”上下文。
 * requested 保存本次调用想要发送/接收的字节数；退出 probe 拿它与
 * 实际完成字节数（ret）对比，即可算出真实吞吐。
 */
struct io_info {
	struct sock *sk; /* 对应的内核 socket 对象指针 */
	__u64 requested; /* 本次调用请求的字节数（send 为要发送数，recv 为缓冲区大小） */
};

/*
 * syscall_start_info：syscall tracepoint 入口与退出之间传递的
 * “系统调用开始”信息。key 是 pid_tgid，value 是开始时间与调用编号。
 */
struct syscall_start_info {
	__u64 timestamp_ns; /* 系统调用入口时刻（纳秒） */
	__u8 syscall_id;    /* 系统调用编号（见 events.h 的 NETDISK_SYSCALL_*） */
	__u8 reserved[7];   /* 补齐到 16 字节，内存对齐友好 */
};

/*
 * events：BPF ring buffer，内核态所有事件统一从这里发给用户态。
 *
 * ring buffer 相比旧 perf buffer 的优势：
 *   - 多 CPU 共享一块内存，无需每个 CPU 一个 buffer；
 *   - 用户态 poll 即可消费，无需 mmap 大块内存；
 *   - 支持 reserve / submit / discard 三步写模型。
 *
 * max_entries = 1 << 24 = 16MB：缓冲总容量。事件生产速度高于消费
 * 速度时，reserve 会返回 NULL（写不进去），此时直接丢弃事件，
 * 不影响业务进程运行。
 */
struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF); /* map 类型：环形缓冲区 */
	__uint(max_entries, 1 << 24);       /* 容量 16MB */
} events SEC(".maps");

/* BPF 程序许可证声明：内核要求 license 可接受才会加载成功 */
char LICENSE[] SEC("license") = "Dual BSD/GPL";

/*
 * connect_info_map：保存“正在进行的 connect”上下文。
 *   key   ：pid_tgid（高 32 位 PID + 低 32 位 TID），用于区分线程；
 *   value ：struct connect_info（入口时间戳 + socket 指针）。
 * 入口 probe 写入，退出 probe 读取后删除。
 */
struct {
	__uint(type, BPF_MAP_TYPE_HASH); /* 指定 map 类型为哈希表 */
	__uint(max_entries, 10240);      /* 最多同时跟踪 10240 个并发 connect */
	__type(key, __u64);              /* key 类型：__u64（pid_tgid） */
	__type(value, struct connect_info); /* value 类型 */
} connect_info_map SEC(".maps");

/*
 * send_info_map：保存“正在进行的 tcp_sendmsg”上下文。
 * 入口 kprobe 写入，退出 kretprobe 读取并删除。
 */
struct {
	__uint(type, BPF_MAP_TYPE_HASH);  /* map 类型：哈希表 */
	__uint(max_entries, 10240);       /* 最多同时跟踪 10240 个并发 send */
	__type(key, __u64);               /* key：pid_tgid */
	__type(value, struct io_info);    /* value：socket 指针 + 请求字节数 */
} send_info_map SEC(".maps");

/*
 * recv_info_map：保存“正在进行的 tcp_recvmsg”上下文。
 * 结构与 send_info_map 完全相同，作用域是接收方向。
 */
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 10240);
	__type(key, __u64);
	__type(value, struct io_info);
} recv_info_map SEC(".maps");

/*
 * syscall_start_map：保存“正在进行中的系统调用”开始信息。
 * key 是 pid_tgid，value 是入口时刻与调用编号。
 * 入口 tracepoint 写入，出口 tracepoint 读取后删除。
 */
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 10240);
	__type(key, __u64);
	__type(value, struct syscall_start_info);
} syscall_start_map SEC(".maps");

/*
 * should_trace：PID 过滤判定。
 * 决定当前事件是否应该继续处理：
 *   - target_pid == 0            → 监控所有进程；
 *   - target_pid == 当前 PID     → 命中目标进程。
 * 其余情况直接返回，不占 ring buffer 空间。
 */
static __always_inline int should_trace(__u32 pid)
{
	return target_pid == 0 || target_pid == pid;
}

/*
 * read_sock_info：从内核 struct sock 中提取四元组信息。
 * 使用 BPF_CORE_READ（CO-RE 机制）读取字段，即使内核结构体
 * 布局随版本变化也能正确偏移，无需针对每个内核版本重新编译。
 *
 * 参数：
 *   sk    ：内核 socket 对象指针；
 *   saddr / daddr / sport / dport：输出参数，用于回填四元组。
 */
static __always_inline void read_sock_info(struct sock *sk,
					   __u32 *saddr, // 源 IPv4 地址
					   __u32 *daddr, // 目标 IPv4 地址
					   __u16 *sport, // 源端口
					   __u16 *dport	 // 目标端口
)
{
	__u16 dport_be; /* 保存未经转换的目标端口（网络字节序） */
	/* skc_rcv_saddr：本机接收地址（网络字节序，即内存中的原始存储顺序） */
	*saddr = BPF_CORE_READ(sk, __sk_common.skc_rcv_saddr);
	/* skc_daddr：远端目标地址 */
	*daddr = BPF_CORE_READ(sk, __sk_common.skc_daddr);
	/* skc_num：本机端口（已是主机字节序） */
	*sport = BPF_CORE_READ(sk, __sk_common.skc_num);

	/* skc_dport 存的是网络字节序，用 bpf_ntohs 转成主机字节序 */
	dport_be = BPF_CORE_READ(sk, __sk_common.skc_dport);
	*dport = bpf_ntohs(dport_be);
}
/*
 * emit_network_event：组装并发送一个网络事件到 ring buffer。
 * 这是所有网络动作的公共出口：connect / accept / close / send / recv
 * 最终都汇聚到这里。
 *
 * 参数：
 *   sk           ：内核 socket 对象指针；
 *   action       ：动作类型（NETDISK_NET_CONNECT 等，见 events.h）；
 *   timestamp_ns ：事件时间戳（纳秒）；
 *   bytes        ：收发字节数（connect/accept/close 时为 0）。
 *
 * 流程：校验 socket → PID 过滤 → 地址族过滤 → reserve 空间 →
 *        填充事件 → submit 提交。
 */
static __always_inline int emit_network_event(struct sock *sk, __u8 action,
					      __u64 timestamp_ns, __u64 bytes)
{
	struct netdisk_event *event; /* 将指向 ring buffer 为本次事件预留的内存 */
	__u64 pid_tgid;              /* 高 32 位 PID + 低 32 位 TID */
	__u32 pid;                   /* 当前进程 PID */
	__u16 family;                /* socket 地址族 */

	/* socket 指针为空说明上下文无效（例如 accept 失败），直接丢弃 */
	if (!sk) {
		return 0;
	}
	pid_tgid = bpf_get_current_pid_tgid();
	pid = pid_tgid >> 32; /* 高 32 位即 PID */

	/* 不在监控范围内的进程直接返回，不产生事件 */
	if (!should_trace(pid)) {
		return 0;
	}
	/* 只关心 IPv4 连接（当前网盘协议为 IPv4 局域网环境） */
	family = BPF_CORE_READ(sk, __sk_common.skc_family);
	if (family != NETDISK_AF_INET) {
		return 0;
	}

	/* 向 ring buffer 申请一块事件大小的内存。
	 * 参数 0 表示不指定 CPU；缓冲区满时返回 NULL，
	 * 此时直接丢弃事件（保证监控自身不阻塞业务）。 */
	event = bpf_ringbuf_reserve(&events, sizeof(*event), 0);
	if (!event) {
		return 0;
	}

	/* 将整块事件内存初始化为 0。
	 * 使用编译器内建 memset：Clang 会把它展开成 verifier
	 * 能分析的写操作，避免普通 memset 调用通不过校验。 */
	__builtin_memset(event, 0, sizeof(*event));

	event->kind = NETDISK_EVENT_NETWORK;  /* 指定事件类型为网络事件 */
	event->network.pid = pid;	      /* 拿到进程 PID */
	event->network.tid = (__u32)pid_tgid; /* 拿到线程 TID */
	event->network.action = action;       /* 写入调用者传来的动作类型 */
	event->network.timestamp_ns = timestamp_ns;
	event->network.bytes = bytes;

	/* 调用 BPF helper 读取当前进程名（comm，最长 15 字符） */
	bpf_get_current_comm(event->network.comm,
			     sizeof(event->network.comm));

	/* 从 socket 中提取四元组（源/目的 IP、端口）回填事件 */
	read_sock_info(sk, &event->network.saddr, &event->network.daddr,
		       &event->network.sport, &event->network.dport);

	/* 提交事件：写入完成，通知用户态可消费。
	 * 注意 reserve 成功后必须 submit 或 discard 二选一，
	 * 否则预留的内存永远不会释放。 */
	bpf_ringbuf_submit(event, 0);

	return 0;
}

/*
 * trace_syscall_entry：系统调用入口 tracepoint 的公共处理函数。
 * 在 sys_enter_* 触发时被调用：记录入口时间戳和调用编号，
 * 存入 syscall_start_map，供出口 tracepoint 配对计算耗时。
 *
 * 参数：syscall_id —— 系统调用编号（events.h 中的 NETDISK_SYSCALL_*）。
 */
static __always_inline int trace_syscall_entry(__u8 syscall_id)
{
	struct syscall_start_info info = {};
	__u64 pid_tgid;
	__u32 pid;

	pid_tgid = bpf_get_current_pid_tgid();
	pid = pid_tgid >> 32;
	/* 只跟踪目标进程的系统调用 */
	if (!should_trace(pid))
		return 0;

	info.timestamp_ns = bpf_ktime_get_ns(); /* 记录入口时刻 */
	info.syscall_id = syscall_id;           /* 记录是哪个系统调用 */
	/* BPF_ANY：存在则覆盖，不存在则新建（同一线程理论上不会嵌套同名调用） */
	bpf_map_update_elem(&syscall_start_map, &pid_tgid, &info, BPF_ANY);
	return 0;
}

/*
 * trace_syscall_exit：系统调用出口 tracepoint 的公共处理函数。
 * 在 sys_exit_* 触发时被调用：
 *   1. 按 pid_tgid 从 map 取出入口信息；
 *   2. 校验编号一致后计算耗时（exit 时刻 - entry 时刻）；
 *   3. 组装 syscall 事件提交到 ring buffer。
 *
 * 参数：
 *   ctx         ：tracepoint 上下文（内含返回值 ret）；
 *   syscall_id  ：本次退出的系统调用编号。
 */
static __always_inline int
trace_syscall_exit(struct trace_event_raw_sys_exit *ctx, __u8 syscall_id)
{
	struct syscall_start_info *info;
	struct netdisk_event *event;
	__u64 pid_tgid;
	__u64 start_ns;       /* 入口时刻 */
	__u64 timestamp_ns;   /* 出口时刻 */
	__u8 stored_syscall_id; /* 入口时记录的调用编号 */

	pid_tgid = bpf_get_current_pid_tgid();
	/* 没有入口记录（例如只挂了出口、或入口被过滤），直接放弃 */
	info = bpf_map_lookup_elem(&syscall_start_map, &pid_tgid);
	if (!info)
		return 0;

	start_ns = info->timestamp_ns;
	stored_syscall_id = info->syscall_id;
	/* 先删除再校验：无论后续是否发事件，入口记录都只配对一次 */
	bpf_map_delete_elem(&syscall_start_map, &pid_tgid);

	/* 编号不一致说明入口/出口错位（异常情况），放弃 */
	if (stored_syscall_id != syscall_id)
		return 0;

	timestamp_ns = bpf_ktime_get_ns();
	/* 向 ring buffer 申请事件内存，满了则丢弃 */
	event = bpf_ringbuf_reserve(&events, sizeof(*event), 0);
	if (!event)
		return 0;

	__builtin_memset(event, 0, sizeof(*event));
	event->kind = NETDISK_EVENT_SYSCALL; /* 事件类型：系统调用 */
	event->syscall.pid = pid_tgid >> 32;
	event->syscall.tid = (__u32)pid_tgid;
	event->syscall.timestamp_ns = timestamp_ns;
	/* 耗时 = 出口时刻 - 入口时刻 */
	event->syscall.duration_ns = timestamp_ns - start_ns;
	event->syscall.ret = ctx->ret; /* 系统调用返回值 */
	event->syscall.syscall_id = syscall_id;
	bpf_get_current_comm(event->syscall.comm,
			     sizeof(event->syscall.comm));
	bpf_ringbuf_submit(event, 0);
	return 0;
}

/*
 * DEFINE_SYSCALL_PROBES：宏批量生成系统调用入口/出口探针。
 *
 * 展开效果（以 read 为例）：
 *   handle_sys_enter_read()  → 调用 trace_syscall_entry(NETDISK_SYSCALL_READ)
 *   handle_sys_exit_read()   → 调用 trace_syscall_exit(ctx, NETDISK_SYSCALL_READ)
 *
 * 参数：
 *   syscall_name    ：系统调用名（同时决定 tracepoint 路径和函数名）；
 *   syscall_id_value：对应的编号常量（NETDISK_SYSCALL_READ 等）。
 *
 * 说明：
 *   - 挂载点是 tracepoint/syscalls/sys_enter_<name> 和 sys_exit_<name>，
 *     比 kprobe 更稳定（是内核对外契约，不随函数名/优化变化）；
 *   - SEC 宏把函数放入 ELF 对应 section，libbpf 据此识别挂载类型；
 *   - 入口函数的 ctx 参数未使用，用 (void)ctx 抑制编译告警。
 */
#define DEFINE_SYSCALL_PROBES(syscall_name, syscall_id_value)                 \
	SEC("tracepoint/syscalls/sys_enter_" #syscall_name)                    \
	int handle_sys_enter_##syscall_name(                                    \
		struct trace_event_raw_sys_enter *ctx)                            \
	{                                                                       \
		(void)ctx; /* 入口阶段暂时不需要 ctx（返回值在出口才有） */       \
		return trace_syscall_entry(syscall_id_value);                     \
	}                                                                       \
	SEC("tracepoint/syscalls/sys_exit_" #syscall_name)                     \
	int handle_sys_exit_##syscall_name(                                     \
		struct trace_event_raw_sys_exit *ctx)                             \
	{                                                                       \
		return trace_syscall_exit(ctx, syscall_id_value);                 \
	}

/* 依次为 7 类目标系统调用生成入口/出口探针 */
DEFINE_SYSCALL_PROBES(read, NETDISK_SYSCALL_READ)
DEFINE_SYSCALL_PROBES(write, NETDISK_SYSCALL_WRITE)
DEFINE_SYSCALL_PROBES(openat, NETDISK_SYSCALL_OPENAT)
DEFINE_SYSCALL_PROBES(close, NETDISK_SYSCALL_CLOSE)
DEFINE_SYSCALL_PROBES(accept4, NETDISK_SYSCALL_ACCEPT4)
DEFINE_SYSCALL_PROBES(sendto, NETDISK_SYSCALL_SENDTO)
DEFINE_SYSCALL_PROBES(recvfrom, NETDISK_SYSCALL_RECVFROM)

/*
 * ───────────────── 网络事件探针 ─────────────────
 */

/*
 * handle_tcp_v4_connect：主动连接（connect 系统调用）入口探针。
 *
 * BPF_KPROBE 宏自动完成 ctx 参数解包：函数签名里直接声明
 * 内核函数 tcp_v4_connect(struct sock *sk, ...) 的对应参数即可。
 *
 * 入口阶段只做两件事：PID 过滤 + 记录起始信息到 connect_info_map，
 * 真正的连接结果在下面的 kretprobe 里处理。
 */
SEC("kprobe/tcp_v4_connect")
int BPF_KPROBE(handle_tcp_v4_connect, struct sock *sk)
{
	__u64 pid_tgid;
	__u32 pid;
	struct connect_info info = {};

	pid_tgid = bpf_get_current_pid_tgid();
	pid = pid_tgid >> 32;

	/* 非目标进程直接忽略，不写 map */
	if (!should_trace(pid)) {
		return 0;
	}

	info.timestamp_ns = bpf_ktime_get_ns(); /* 记录连接发起时刻 */
	info.sk = sk;                           /* 保存 socket 指针 */

	/* 以 pid_tgid 为 key 暂存，等待退出 probe 读取 */
	bpf_map_update_elem(&connect_info_map, &pid_tgid, &info, BPF_ANY);

	return 0;
}

/*
 * handle_tcp_v4_connect_ret：主动连接退出探针（kretprobe）。
 *
 * kretprobe 的参数只有返回值 ret：
 *   - ret == 0             ：连接立即成功；
 *   - ret == -EINPROGRESS  ：非阻塞 socket 连接正在建立，也属正常；
 *   - 其他值               ：连接失败，丢弃事件。
 *
 * 从 map 中取出入口时保存的 socket 与时间戳，发出 CONNECT 事件。
 */
SEC("kretprobe/tcp_v4_connect")
int BPF_KRETPROBE(handle_tcp_v4_connect_ret, int ret)
{
	struct connect_info *info;
	struct sock *sk;
	__u64 pid_tgid;
	__u64 timestamp_ns;

	pid_tgid = bpf_get_current_pid_tgid();

	/* 没有入口记录（可能入口被过滤或 map 被挤掉），放弃 */
	info = bpf_map_lookup_elem(&connect_info_map, &pid_tgid);

	if (!info) {
		return 0;
	}

	sk = info->sk;
	timestamp_ns = info->timestamp_ns;
	/* 配对完成，删除记录防止残留 */
	bpf_map_delete_elem(&connect_info_map, &pid_tgid);

	/* 只报告成功与"连接中"，失败的 connect 不上报 */
	if (ret != 0 && ret != -NETDISK_EINPROGRESS) {
		return 0;
	}

	/* bytes=0：connect 动作没有收发字节 */
	return emit_network_event(sk, NETDISK_NET_CONNECT, timestamp_ns, 0);
}

/*
 * handle_inet_csk_accept_ret：被动接受连接退出探针。
 * inet_csk_accept() 是被 accept 系统调用调用的内核函数，
 * 返回值是新连接的 struct sock *。连接建立（三次握手完成）后
 * 此处才会返回，发出 ACCEPT 事件。
 */
SEC("kretprobe/inet_csk_accept")
int BPF_KRETPROBE(handle_inet_csk_accept_ret, struct sock *sk)
{
	/* accept 队列为空返回 NULL，属正常现象，忽略 */
	if (!sk) {
		return 0;
	}

	/* 发出 ACCEPT 事件，时间戳取当前时刻 */
	return emit_network_event(sk, NETDISK_NET_ACCEPT, bpf_ktime_get_ns(), 0);
}

/*
 * handle_tcp_close：关闭连接探针。
 * tcp_close() 在连接完全关闭时被调用，发出 CLOSE 事件。
 */
SEC("kprobe/tcp_close")
int BPF_KPROBE(handle_tcp_close, struct sock *sk)
{
	return emit_network_event(sk, NETDISK_NET_CLOSE, bpf_ktime_get_ns(), 0);
}

/*
 * handle_tcp_sendmsg：发送数据入口探针。
 * tcp_sendmsg(sk, msg, size) 是 TCP 发送路径的入口：
 *   - size：本次调用要发送的字节数（请求值）；
 * 入口阶段只记录请求值与 socket 指针，实际发送量由退出探针
 * 的返回值决定。
 */
SEC("kprobe/tcp_sendmsg")
int BPF_KPROBE(handle_tcp_sendmsg, struct sock *sk, struct msghdr *msg,
	       size_t size)
{
	struct io_info info = {};
	__u64 pid_tgid;
	__u32 pid;

	(void)msg; /* msghdr 内容当前不需要，抑制未使用告警 */
	if (!sk)
		return 0;

	pid_tgid = bpf_get_current_pid_tgid();
	pid = pid_tgid >> 32;
	/* PID 过滤 */
	if (!should_trace(pid))
		return 0;

	info.sk = sk;
	info.requested = size; /* 记录本次请求发送的字节数 */
	/* 暂存到 send_info_map，等待退出探针读取 */
	bpf_map_update_elem(&send_info_map, &pid_tgid, &info, BPF_ANY);
	return 0;
}

/*
 * handle_tcp_sendmsg_ret：发送数据退出探针。
 * kretprobe 返回值 ret = 实际发送的字节数：
 *   - ret > 0 ：正常，发出 SEND 事件（bytes = ret）；
 *   - ret <= 0：失败或没有数据，丢弃。
 */
SEC("kretprobe/tcp_sendmsg")
int BPF_KRETPROBE(handle_tcp_sendmsg_ret, int ret)
{
	struct io_info *info;
	struct sock *sk;
	__u64 pid_tgid;

	pid_tgid = bpf_get_current_pid_tgid();
	/* 没有入口记录则放弃 */
	info = bpf_map_lookup_elem(&send_info_map, &pid_tgid);
	if (!info)
		return 0;

	sk = info->sk;
	/* 配对完成，删除记录 */
	bpf_map_delete_elem(&send_info_map, &pid_tgid);
	/* 发送失败（ret<=0）不上报 */
	if (ret <= 0)
		return 0;

	/* 发出 SEND 事件，bytes 为实际发送字节数 */
	return emit_network_event(sk, NETDISK_NET_SEND, bpf_ktime_get_ns(),
				  (__u64)ret);
}

/*
 * handle_tcp_recvmsg：接收数据入口探针。
 * tcp_recvmsg(sk, msg, len) 是 TCP 接收路径的入口：
 *   - len：接收缓冲区大小（请求接收的最大字节数）；
 * 入口只记录请求值与 socket 指针，实际收到多少由退出探针决定。
 */
SEC("kprobe/tcp_recvmsg")
int BPF_KPROBE(handle_tcp_recvmsg, struct sock *sk, struct msghdr *msg,
	       size_t len)
{
	struct io_info info = {};
	__u64 pid_tgid;
	__u32 pid;

	(void)msg; /* msghdr 内容当前不需要 */
	if (!sk)
		return 0;

	pid_tgid = bpf_get_current_pid_tgid();
	pid = pid_tgid >> 32;
	/* PID 过滤 */
	if (!should_trace(pid))
		return 0;

	info.sk = sk;
	info.requested = len; /* 记录请求接收的字节数（缓冲区大小） */
	/* 暂存到 recv_info_map，等待退出探针读取 */
	bpf_map_update_elem(&recv_info_map, &pid_tgid, &info, BPF_ANY);
	return 0;
}

/*
 * handle_tcp_recvmsg_ret：接收数据退出探针。
 * kretprobe 返回值 ret = 实际收到的字节数：
 *   - ret > 0 ：正常，发出 RECV 事件（bytes = ret）；
 *   - ret <= 0：没有数据（对端关闭返回 0）或出错，丢弃。
 */
SEC("kretprobe/tcp_recvmsg")
int BPF_KRETPROBE(handle_tcp_recvmsg_ret, int ret)
{
	struct io_info *info;
	struct sock *sk;
	__u64 pid_tgid;

	pid_tgid = bpf_get_current_pid_tgid();
	/* 没有入口记录则放弃 */
	info = bpf_map_lookup_elem(&recv_info_map, &pid_tgid);
	if (!info)
		return 0;

	sk = info->sk;
	/* 配对完成，删除记录 */
	bpf_map_delete_elem(&recv_info_map, &pid_tgid);
	/* 无数据或出错不上报 */
	if (ret <= 0)
		return 0;

	/* 发出 RECV 事件，bytes 为实际收到字节数 */
	return emit_network_event(sk, NETDISK_NET_RECV, bpf_ktime_get_ns(),
				  (__u64)ret);
}
