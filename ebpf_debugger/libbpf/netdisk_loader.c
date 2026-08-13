/*
 * netdisk_loader.c —— 网盘 eBPF 监控的用户态加载器
 *
 * 职责：
 *   1. 解析命令行参数（选择 network / syscall 模式与 PID 过滤）；
 *   2. 打开 skeleton → 按模式配置程序自动加载 → load → attach；
 *   3. 创建 ring buffer，轮询内核态事件；
 *   4. 把每条事件格式化为单行 JSON 输出到 stdout，
 *      供上层 Python 采集器（ebpf_debugger）直接按行解析。
 *
 * 典型用法：
 *   sudo ./netdisk_loader --network            # 监控所有进程的网络事件
 *   sudo ./netdisk_loader --syscall --pid 1234 # 只监控 PID 1234 的系统调用
 *
 * 编译：由 Makefile 链接 libbpf / libelf / zlib，
 * 并依赖 bpftool 从 netdisk_bpf.bpf.o 生成的 netdisk_bpf.skel.h。
 */

#include <arpa/inet.h>  /* inet_ntop：网络地址转字符串 */
#include <errno.h>      /* errno 与 EINTR */
#include <getopt.h>     /* getopt_long：解析长选项 */
#include <inttypes.h>   /* PRIu64 / PRId64：跨平台整数打印格式 */
#include <signal.h>     /* signal / sig_atomic_t：处理 Ctrl+C 信号 */
#include <stdbool.h>    /* bool 类型 */
#include <stdint.h>     /* 定长整数类型 */
#include <stdarg.h>     /* va_list：可变参数（libbpf 日志回调） */
#include <stddef.h>     /* NULL / size_t */
#include <stdio.h>      /* 标准输入输出（stdout 输出 JSON，stderr 输出日志） */
#include <stdlib.h>     /* strtoul / EXIT_SUCCESS 等 */
#include <string.h>     /* 字符串操作 */

#include <bpf/libbpf.h>          /* libbpf：用户态加载 BPF 程序的核心库 */

#include "include/events.h"      /* 事件结构体与枚举（与内核态共享） */
#include "netdisk_bpf.skel.h"    /* 由 bpftool gen skeleton 生成，
				  * 内含 netdisk_bpf_bpf__open/load/attach 等接口 */

/*
 * exiting：全局退出标志。
 * 声明为 volatile sig_atomic_t 以保证信号处理器中读写是原子且
 * 立即可见的（信号处理器只能安全地修改这种类型）。
 */
static volatile sig_atomic_t exiting;

/*
 * loader_options：命令行选项的集合。
 *   network ：是否启用网络事件监控（--network）；
 *   syscall ：是否启用系统调用监控（--syscall）；
 *   target_pid：PID 过滤值（--pid，0 表示全部进程）。
 */
struct loader_options {
	bool network;
	bool syscall;
	__u32 target_pid;
};

/*
 * handle_signal：SIGINT / SIGTERM 信号处理器。
 * 只置位退出标志，让主循环中的 ring_buffer__poll 自然返回，
 * 从而能走正常的资源释放流程（free ring buffer、destroy skeleton）。
 */
static void handle_signal(int signal_number)
{
	(void)signal_number; /* 不关心具体是哪个信号 */
	exiting = 1;         /* 置位退出标志 */
}

/*
 * libbpf_log：libbpf 的日志回调。
 * 通过 libbpf_set_print() 注册后，libbpf 内部的所有诊断信息
 * 都会转发到这里。本实现统一输出到 stderr，
 * 保证 stdout 只承载 JSON 事件流，互不污染。
 */
static int libbpf_log(enum libbpf_print_level level, const char *format,
		      va_list args)
{
	(void)level; /* 当前不区分日志级别 */
	return vfprintf(stderr, format, args);
}

/*
 * usage：打印命令行用法。
 * stream 参数允许输出到 stdout（--help 时）或 stderr（出错时）。
 */
static void usage(FILE *stream, const char *program)
{
	fprintf(stream, "Usage: %s (--network | --syscall) [--pid PID]\n",
		program);
}

/*
 * parse_pid：解析并校验 --pid 参数。
 * 规则：
 *   - 必须为纯数字（首字符不是数字直接拒绝）；
 *   - strtoul 必须完整消费字符串（*end == '\0'）；
 *   - 不能溢出（errno == ERANGE 或值超过 UINT32_MAX）；
 *   - 不允许 0（0 是"监控全部"的默认语义，由 target_pid 的
 *     默认值表达，显式传 0 视为非法输入）。
 * 成功返回 0，失败返回 -EINVAL。
 */
static int parse_pid(const char *text, __u32 *pid)
{
	char *end;
	unsigned long value;

	if (!text || text[0] < '0' || text[0] > '9')
		return -EINVAL;

	errno = 0; /* 清空 errno，用 ERANGE 检测数值溢出 */
	value = strtoul(text, &end, 10);
	if (errno || *end != '\0' || value == 0 || value > UINT32_MAX)
		return -EINVAL;

	*pid = (__u32)value;
	return 0;
}

/*
 * parse_args：解析命令行参数。
 *
 * 支持选项：
 *   -n / --network：网络事件模式；
 *   -s / --syscall：系统调用模式（与 --network 二选一）；
 *   -p / --pid PID：PID 过滤；
 *   -h / --help   ：打印帮助。
 *
 * 返回值：
 *   0  ：解析成功；
 *   >0 ：用户请求 --help（main 中按成功退出）；
 *   <0 ：参数错误（main 中按失败退出）。
 */
static int parse_args(int argc, char **argv, struct loader_options *options)
{
	static const struct option long_options[] = {
	    {"network", no_argument, NULL, 'n'},
	    {"syscall", no_argument, NULL, 's'},
	    {"pid", required_argument, NULL, 'p'},
	    {"help", no_argument, NULL, 'h'},
	    {NULL, 0, NULL, 0}, /* 数组必须以全零元素结尾 */
	};
	int option;

	opterr = 0; /* 关闭 getopt 自动报错，改由我们自己统一打印用法 */
	while ((option = getopt_long(argc, argv, "nsp:h", long_options, NULL)) !=
	       -1) {
		switch (option) {
		case 'n':
			options->network = true;
			break;
		case 's':
			options->syscall = true;
			break;
		case 'p':
			/* PID 非法则直接报错退出 */
			if (parse_pid(optarg, &options->target_pid)) {
				fprintf(stderr, "invalid PID: %s\n", optarg);
				usage(stderr, argv[0]);
				return -EINVAL;
			}
			break;
		case 'h':
			usage(stdout, argv[0]);
			return 1;
		default:
			usage(stderr, argv[0]);
			return -EINVAL;
		}
	}

	/* 校验约束：
	 *   - network 与 syscall 必须恰好二选一
	 *     （相等意味着全为 false 或全为 true，均为非法）；
	 *   - optind == argc 表示没有多余的位置参数。 */
	if (options->network == options->syscall || optind != argc) {
		usage(stderr, argv[0]);
		return -EINVAL;
	}

	return 0;
}

/*
 * configure_programs：按所选模式配置哪些 BPF 程序自动加载。
 *
 * 流程：
 *   1. 先把对象里所有程序设为不自动加载（autoload = false）；
 *   2. 根据 --network / --syscall 选择对应的程序数组；
 *   3. 只把选中程序重新设为 autoload = true；
 *   4. 在 load 之前写入 rodata 段的 target_pid（PID 过滤值）。
 *
 * 为什么要逐程序控制 autoload：
 *   skeleton 把网络探针与系统调用探针全部编译在同一个 .bpf.o 里，
 *   若全部 attach，挂载点过多、开销大且事件混杂。按模式只加载
 *   一组程序，可显著减少不必要的 kprobe/tracepoint。
 */
static int configure_programs(struct netdisk_bpf_bpf *skel,
			      const struct loader_options *options)
{
	/* 网络模式需要加载的 8 个程序（入口/出口成对） */
	struct bpf_program *network_programs[] = {
	    skel->progs.handle_tcp_v4_connect,     /* connect 入口 */
	    skel->progs.handle_tcp_v4_connect_ret, /* connect 出口 */
	    skel->progs.handle_inet_csk_accept_ret, /* accept 出口 */
	    skel->progs.handle_tcp_close,          /* close */
	    skel->progs.handle_tcp_sendmsg,        /* send 入口 */
	    skel->progs.handle_tcp_sendmsg_ret,    /* send 出口 */
	    skel->progs.handle_tcp_recvmsg,        /* recv 入口 */
	    skel->progs.handle_tcp_recvmsg_ret,    /* recv 出口 */
	};
	/* 系统调用模式需要加载的 14 个程序（7 类调用 × 入口/出口） */
	struct bpf_program *syscall_programs[] = {
	    skel->progs.handle_sys_enter_read,
	    skel->progs.handle_sys_exit_read,
	    skel->progs.handle_sys_enter_write,
	    skel->progs.handle_sys_exit_write,
	    skel->progs.handle_sys_enter_openat,
	    skel->progs.handle_sys_exit_openat,
	    skel->progs.handle_sys_enter_close,
	    skel->progs.handle_sys_exit_close,
	    skel->progs.handle_sys_enter_accept4,
	    skel->progs.handle_sys_exit_accept4,
	    skel->progs.handle_sys_enter_sendto,
	    skel->progs.handle_sys_exit_sendto,
	    skel->progs.handle_sys_enter_recvfrom,
	    skel->progs.handle_sys_exit_recvfrom,
	};
	struct bpf_program **selected_programs; /* 指向最终选中的程序数组 */
	struct bpf_program *program;
	size_t selected_program_count;          /* 选中数组的元素个数 */
	size_t index;
	int err;

	/* 第一遍：遍历对象中的所有程序，全部关闭自动加载 */
	bpf_object__for_each_program(program, skel->obj) {
		err = bpf_program__set_autoload(program, false);
		if (err)
			return err;
	}

	/* 根据命令行模式选中对应数组 */
	if (options->network) {
		selected_programs = network_programs;
		selected_program_count = sizeof(network_programs) /
					 sizeof(network_programs[0]);
	} else {
		selected_programs = syscall_programs;
		selected_program_count = sizeof(syscall_programs) /
					 sizeof(syscall_programs[0]);
	}

	/* 第二遍：只把选中的程序重新打开自动加载 */
	for (index = 0; index < selected_program_count; index++) {
		err = bpf_program__set_autoload(selected_programs[index], true);
		if (err)
			return err;
	}

	/* 关键：必须在 load 之前设置 rodata 变量。
	 * target_pid 在内核态是只读全局变量（见 netdisk_bpf.bpf.c），
	 * 加载后无法修改；此处写入 --pid 解析出的过滤值。 */
	skel->rodata->target_pid = options->target_pid;
	return 0;
}

/*
 * print_json_string：按 JSON 字符串规范输出一段文本（含转义）。
 *
 * 为什么不能直接 %s 打印：
 *   BPF 事件里的 comm（进程名）等字符数组可能包含引号、反斜杠、
 *   控制字符，直接拼接会破坏 JSON 结构，导致上层 Python
 *   按行 json.loads 解析失败。
 *
 * 本函数逐个字符检查：
 *   - JSON 必需转义：双引号、反斜杠、\b \f \n \r \t；
 *   - 其余控制字符与高位字节：统一转成 \u00xx 形式；
 *   - 普通可打印字符：原样输出。
 *
 * 参数：
 *   text       ：待输出的字符串；
 *   max_length ：最大检查长度（comm 是定长数组，防止越界读）。
 */
static void print_json_string(const char *text, size_t max_length)
{
	size_t i;

	fputc('"', stdout); /* 输出开头的双引号 */
	for (i = 0; i < max_length && text[i] != '\0'; i++) {
		unsigned char character = (unsigned char)text[i];

		switch (character) {
		case '"':
			fputs("\\\"", stdout); /* 双引号 → \" */
			break;
		case '\\':
			fputs("\\\\", stdout); /* 反斜杠 → \\ */
			break;
		case '\b':
			fputs("\\b", stdout);
			break;
		case '\f':
			fputs("\\f", stdout);
			break;
		case '\n':
			fputs("\\n", stdout);
			break;
		case '\r':
			fputs("\\r", stdout);
			break;
		case '\t':
			fputs("\\t", stdout);
			break;
		default:
			/* 其他控制字符（< 0x20）和高位字节（>= 0x7f）
			 * 一律转义为 \uXXXX，保证输出是纯 ASCII JSON */
			if (character < 0x20 || character >= 0x7f)
				fprintf(stdout, "\\u%04x",
					(unsigned int)character);
			else
				fputc(character, stdout);
		}
	}
	fputc('"', stdout); /* 输出结尾的双引号 */
}

/*
 * syscall_name：把内核态传来的系统调用编号转换成可读字符串。
 * 编号定义见 events.h 的 NETDISK_SYSCALL_* 枚举。
 * 未知编号返回 NULL，由调用方按异常处理。
 */
static const char *syscall_name(__u8 syscall_id)
{
	switch (syscall_id) {
	case NETDISK_SYSCALL_READ:
		return "read";
	case NETDISK_SYSCALL_WRITE:
		return "write";
	case NETDISK_SYSCALL_OPENAT:
		return "openat";
	case NETDISK_SYSCALL_CLOSE:
		return "close";
	case NETDISK_SYSCALL_ACCEPT4:
		return "accept4";
	case NETDISK_SYSCALL_SENDTO:
		return "sendto";
	case NETDISK_SYSCALL_RECVFROM:
		return "recvfrom";
	default:
		return NULL;
	}
}

/*
 * handle_network_event：把一条网络事件格式化为 JSON 输出到 stdout。
 *
 * 输出示例：
 *   {"type":"network","action":1,"pid":123,"tid":123,"comm":"server",
 *    "saddr":"127.0.0.1","daddr":"127.0.0.1","sport":9000,"dport":5,
 *    "bytes":4096,"timestamp_ns":123456789}
 *
 * 校验：
 *   - action 必须在合法枚举范围内；
 *   - 两个 IP 地址都必须能转成点分十进制字符串。
 * 校验失败只打印诊断并丢弃本条（返回 0，不影响事件循环）。
 */
static int handle_network_event(const struct netdisk_network_event *network)
{
	char source_address[INET_ADDRSTRLEN];      /* 源 IP 字符串缓冲 */
	char destination_address[INET_ADDRSTRLEN]; /* 目的 IP 字符串缓冲 */

	/* 防御性校验：action 超出已知范围说明数据损坏 */
	if (network->action < NETDISK_NET_CONNECT ||
	    network->action > NETDISK_NET_RECV) {
		fprintf(stderr, "invalid network action: %u\n",
			(unsigned int)network->action);
		return 0;
	}

	/* inet_ntop 把网络字节序的 __u32 地址转成 "x.x.x.x" 字符串 */
	if (!inet_ntop(AF_INET, &network->saddr, source_address,
		       sizeof(source_address)) ||
	    !inet_ntop(AF_INET, &network->daddr, destination_address,
		       sizeof(destination_address))) {
		fprintf(stderr, "failed to convert IPv4 address: %s\n",
			strerror(errno));
		return 0;
	}

	/* 先输出固定字段，comm 字段交给 print_json_string 处理转义 */
	fprintf(stdout,
		"{\"type\":\"network\",\"action\":%u,\"pid\":%u,"
		"\"tid\":%u,\"comm\":",
		(unsigned int)network->action, network->pid, network->tid);
	print_json_string(network->comm, sizeof(network->comm));
	/* 其余字段（地址/端口/字节数/时间戳）拼完并换行 */
	fprintf(stdout,
		",\"saddr\":\"%s\",\"daddr\":\"%s\",\"sport\":%u,"
		"\"dport\":%u,\"bytes\":%" PRIu64 ",\"timestamp_ns\":%" PRIu64
		"}\n",
		source_address, destination_address,
		(unsigned int)network->sport, (unsigned int)network->dport,
		(uint64_t)network->bytes, (uint64_t)network->timestamp_ns);
	fflush(stdout); /* 立即刷出，保证管道另一端的采集器能实时读到 */

	return 0;
}

/*
 * handle_syscall_event：把一条系统调用事件格式化为 JSON 输出到 stdout。
 *
 * 输出示例：
 *   {"type":"syscall","pid":123,"tid":123,"comm":"server",
 *    "syscall":"read","duration_ns":4500,"ret":128,"timestamp_ns":123456789}
 *
 * syscall_id 未知时打印诊断并丢弃本条。
 */
static int handle_syscall_event(const struct netdisk_syscall_event *syscall)
{
	const char *name;

	name = syscall_name(syscall->syscall_id);
	if (!name) {
		fprintf(stderr, "invalid syscall id: %u\n",
			(unsigned int)syscall->syscall_id);
		return 0;
	}

	/* 与网络事件相同的两段式打印：先固定字段，再输出转义后的 comm */
	fprintf(stdout,
		"{\"type\":\"syscall\",\"pid\":%u,\"tid\":%u,\"comm\":",
		syscall->pid, syscall->tid);
	print_json_string(syscall->comm, sizeof(syscall->comm));
	/* ret 用 PRId64 支持打印负数（失败的返回值为负 errno） */
	fprintf(stdout,
		",\"syscall\":\"%s\",\"duration_ns\":%" PRIu64
		",\"ret\":%" PRId64 ",\"timestamp_ns\":%" PRIu64 "}\n",
		name, (uint64_t)syscall->duration_ns, (int64_t)syscall->ret,
		(uint64_t)syscall->timestamp_ns);
	fflush(stdout); /* 立即刷出 */
	return 0;
}

/*
 * handle_event：ring buffer 消费回调（每条事件触发一次）。
 *
 * ring_buffer__poll() 每次从内核读出一条事件就调用本函数：
 *   1. 校验数据指针与长度（防御性检查）；
 *   2. 根据事件 kind 分派到对应的处理函数。
 *
 * 返回值 0 表示继续；返回负值会让 poll 停止（本实现始终返回 0，
 * 退出由 exiting 标志控制）。
 */
static int handle_event(void *ctx, void *data, size_t data_sz)
{
	const struct netdisk_event *event = data;

	(void)ctx; /* ring buffer 回调的 ctx 未使用 */

	/* 防御性校验：指针为空或长度不等于事件结构体大小都视为异常 */
	if (!data || data_sz != sizeof(*event)) {
		fprintf(stderr,
			"invalid event size: got %zu bytes, expected %zu\n",
			data_sz, sizeof(*event));
		return 0;
	}

	/* 按事件类型分派 */
	switch (event->kind) {
	case NETDISK_EVENT_NETWORK:
		return handle_network_event(&event->network);
	case NETDISK_EVENT_SYSCALL:
		return handle_syscall_event(&event->syscall);
	default:
		fprintf(stderr, "unsupported event kind: %u\n", event->kind);
		return 0;
	}
}

/*
 * main：loader 主流程。
 *
 * 完整生命周期：
 *   解析参数 → 注册日志回调 → 注册信号处理器 → open skeleton
 *   → configure（按模式选程序 + 写 PID） → load → attach
 *   → 创建 ring buffer → 轮询事件循环 → cleanup 释放资源。
 *
 * open/load/attach 三个阶段：
 *   - open ：把 .bpf.o 读入内存，构造 skeleton 结构，此时可改 rodata；
 *   - load ：把 BPF 程序送进内核并校验（verifier 在此阶段执行）；
 *   - attach：把程序挂到 kprobe/tracepoint 上，开始生效。
 */
int main(int argc, char **argv)
{
	struct netdisk_bpf_bpf *skel = NULL; /* BPF skeleton：封装所有内核对象 */
	struct ring_buffer *ringbuf = NULL;  /* 用户态 ring buffer 消费端 */
	struct loader_options options = {};  /* 命令行选项（零初始化） */
	int err;

	/* 解析命令行；<0 参数错误，>0 是 --help（视为正常退出） */
	err = parse_args(argc, argv, &options);
	if (err < 0)
		return EXIT_FAILURE;
	if (err > 0)
		return EXIT_SUCCESS;

	/* 注册日志回调：libbpf 的诊断信息统一走 stderr */
	libbpf_set_print(libbpf_log);

	/* 注册 Ctrl+C(SIGINT) 与 kill(SIGTERM) 处理器，支持优雅退出 */
	signal(SIGINT, handle_signal);
	signal(SIGTERM, handle_signal);

	/* 阶段一：open。解析 .bpf.o 生成 skeleton，
	 * 此时 rodata（target_pid）还没有固化进内核。 */
	skel = netdisk_bpf_bpf__open();
	if (!skel) {
		fprintf(stderr, "failed to open BPF skeleton\n");
		return EXIT_FAILURE;
	}

	/* open 之后、load 之前：配置哪些程序要加载，并写入 PID 过滤值 */
	err = configure_programs(skel, &options);
	if (err) {
		fprintf(stderr, "failed to configure BPF programs: %d\n", err);
		goto cleanup;
	}

	/* 阶段二：load。把程序提交内核做 verifier 校验。
	 * 从此之后 rodata 不可再修改。 */
	err = netdisk_bpf_bpf__load(skel);
	if (err) {
		fprintf(stderr, "failed to load BPF skeleton: %d\n", err);
		goto cleanup;
	}

	/* 阶段三：attach。把程序挂到对应的 kprobe/tracepoint，
	 * 监控正式开始生效。 */
	err = netdisk_bpf_bpf__attach(skel);
	if (err) {
		fprintf(stderr, "failed to attach BPF skeleton: %d\n", err);
		goto cleanup;
	}

	/* 创建 ring buffer 消费端：
	 *   bpf_map__fd(skel->maps.events) —— 内核 events map 的 fd；
	 *   handle_event —— 每条事件的回调；
	 *   后两个 NULL —— 回调 ctx 与私有数据，当前不用。 */
	ringbuf = ring_buffer__new(bpf_map__fd(skel->maps.events), handle_event,
				   NULL, NULL);
	if (!ringbuf) {
		err = errno ? -errno : -1;
		fprintf(stderr, "failed to create ring buffer: %d\n", err);
		goto cleanup;
	}

	/* 启动日志走 stderr，避免污染 stdout 的 JSON 流 */
	fprintf(stderr,
		"netdisk loader started in %s mode (PID filter: %u); "
		"press Ctrl+C to stop\n",
		options.network ? "network" : "syscall", options.target_pid);

	/* 主事件循环：轮询 ring buffer，直到收到退出信号 */
	while (!exiting) {
		/* 每次 poll 最多等 100ms；超时返回 0 继续下一轮，
		 * 借此周期检查 exiting 标志，实现响应式退出。 */
		err = ring_buffer__poll(ringbuf, 100);
		if (err == -EINTR) {
			/* poll 被信号打断：若是退出信号则结束循环，
			 * 否则继续（保证信号处理后还能恢复工作）。 */
			if (exiting)
				break;
			continue;
		}
		if (err < 0) {
			/* 其他不可恢复错误：报错并清理退出 */
			fprintf(stderr, "ring buffer poll failed: %d\n", err);
			goto cleanup;
		}
	}

	err = 0; /* 正常退出路径 */
	fprintf(stderr, "netdisk loader stopped\n");

cleanup:
	/* 统一清理：先释放 ring buffer 消费端，再销毁 skeleton
	 * （销毁会自动 detach 所有程序并释放内核资源） */
	ring_buffer__free(ringbuf);
	netdisk_bpf_bpf__destroy(skel);

	/* 把负的 errno 转换成正数进程退出码（0~255 范围） */
	return err < 0 ? -err : err;
}
