#include <arpa/inet.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <bpf/libbpf.h>

#include "include/events.h"
#include "netdisk_bpf.skel.h"

static volatile sig_atomic_t exiting;

struct loader_options {
	bool network;
	bool syscall;
	__u32 target_pid;
};

static void handle_signal(int signal_number)
{
	(void)signal_number;
	exiting = 1;
}

static int libbpf_log(enum libbpf_print_level level, const char *format,
		      va_list args)
{
	(void)level;
	return vfprintf(stderr, format, args);
}

static void usage(FILE *stream, const char *program)
{
	fprintf(stream, "Usage: %s (--network | --syscall) [--pid PID]\n",
		program);
}

static int parse_pid(const char *text, __u32 *pid)
{
	char *end;
	unsigned long value;

	if (!text || text[0] < '0' || text[0] > '9')
		return -EINVAL;

	errno = 0;
	value = strtoul(text, &end, 10);
	if (errno || *end != '\0' || value == 0 || value > UINT32_MAX)
		return -EINVAL;

	*pid = (__u32)value;
	return 0;
}

static int parse_args(int argc, char **argv, struct loader_options *options)
{
	static const struct option long_options[] = {
	    {"network", no_argument, NULL, 'n'},
	    {"syscall", no_argument, NULL, 's'},
	    {"pid", required_argument, NULL, 'p'},
	    {"help", no_argument, NULL, 'h'},
	    {NULL, 0, NULL, 0},
	};
	int option;

	opterr = 0;
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

	if (options->network == options->syscall || optind != argc) {
		usage(stderr, argv[0]);
		return -EINVAL;
	}

	return 0;
}

static int configure_programs(struct netdisk_bpf_bpf *skel,
			      const struct loader_options *options)
{
	struct bpf_program *network_programs[] = {
	    skel->progs.handle_tcp_v4_connect,
	    skel->progs.handle_tcp_v4_connect_ret,
	    skel->progs.handle_inet_csk_accept_ret,
	    skel->progs.handle_tcp_close,
	    skel->progs.handle_tcp_sendmsg,
	    skel->progs.handle_tcp_sendmsg_ret,
	    skel->progs.handle_tcp_recvmsg,
	    skel->progs.handle_tcp_recvmsg_ret,
	};
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
	struct bpf_program **selected_programs;
	struct bpf_program *program;
	size_t selected_program_count;
	size_t index;
	int err;

	bpf_object__for_each_program(program, skel->obj) {
		err = bpf_program__set_autoload(program, false);
		if (err)
			return err;
	}

	if (options->network) {
		selected_programs = network_programs;
		selected_program_count = sizeof(network_programs) /
					 sizeof(network_programs[0]);
	} else {
		selected_programs = syscall_programs;
		selected_program_count = sizeof(syscall_programs) /
					 sizeof(syscall_programs[0]);
	}

	for (index = 0; index < selected_program_count; index++) {
		err = bpf_program__set_autoload(selected_programs[index], true);
		if (err)
			return err;
	}

	skel->rodata->target_pid = options->target_pid;
	return 0;
}

static void print_json_string(const char *text, size_t max_length)
{
	size_t i;

	fputc('"', stdout);
	for (i = 0; i < max_length && text[i] != '\0'; i++) {
		unsigned char character = (unsigned char)text[i];

		switch (character) {
		case '"':
			fputs("\\\"", stdout);
			break;
		case '\\':
			fputs("\\\\", stdout);
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
			if (character < 0x20 || character >= 0x7f)
				fprintf(stdout, "\\u%04x",
					(unsigned int)character);
			else
				fputc(character, stdout);
		}
	}
	fputc('"', stdout);
}

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

static int handle_network_event(const struct netdisk_network_event *network)
{
	char source_address[INET_ADDRSTRLEN];
	char destination_address[INET_ADDRSTRLEN];

	if (network->action < NETDISK_NET_CONNECT ||
	    network->action > NETDISK_NET_RECV) {
		fprintf(stderr, "invalid network action: %u\n",
			(unsigned int)network->action);
		return 0;
	}

	if (!inet_ntop(AF_INET, &network->saddr, source_address,
		       sizeof(source_address)) ||
	    !inet_ntop(AF_INET, &network->daddr, destination_address,
		       sizeof(destination_address))) {
		fprintf(stderr, "failed to convert IPv4 address: %s\n",
			strerror(errno));
		return 0;
	}

	fprintf(stdout,
		"{\"type\":\"network\",\"action\":%u,\"pid\":%u,"
		"\"tid\":%u,\"comm\":",
		(unsigned int)network->action, network->pid, network->tid);
	print_json_string(network->comm, sizeof(network->comm));
	fprintf(stdout,
		",\"saddr\":\"%s\",\"daddr\":\"%s\",\"sport\":%u,"
		"\"dport\":%u,\"bytes\":%" PRIu64 ",\"timestamp_ns\":%" PRIu64
		"}\n",
		source_address, destination_address,
		(unsigned int)network->sport, (unsigned int)network->dport,
		(uint64_t)network->bytes, (uint64_t)network->timestamp_ns);
	fflush(stdout);

	return 0;
}

static int handle_syscall_event(const struct netdisk_syscall_event *syscall)
{
	const char *name;

	name = syscall_name(syscall->syscall_id);
	if (!name) {
		fprintf(stderr, "invalid syscall id: %u\n",
			(unsigned int)syscall->syscall_id);
		return 0;
	}

	fprintf(stdout,
		"{\"type\":\"syscall\",\"pid\":%u,\"tid\":%u,\"comm\":",
		syscall->pid, syscall->tid);
	print_json_string(syscall->comm, sizeof(syscall->comm));
	fprintf(stdout,
		",\"syscall\":\"%s\",\"duration_ns\":%" PRIu64
		",\"ret\":%" PRId64 ",\"timestamp_ns\":%" PRIu64 "}\n",
		name, (uint64_t)syscall->duration_ns, (int64_t)syscall->ret,
		(uint64_t)syscall->timestamp_ns);
	fflush(stdout);
	return 0;
}

static int handle_event(void *ctx, void *data, size_t data_sz)
{
	const struct netdisk_event *event = data;

	(void)ctx;

	if (!data || data_sz != sizeof(*event)) {
		fprintf(stderr,
			"invalid event size: got %zu bytes, expected %zu\n",
			data_sz, sizeof(*event));
		return 0;
	}

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

int main(int argc, char **argv)
{
	struct netdisk_bpf_bpf *skel = NULL;
	struct ring_buffer *ringbuf = NULL;
	struct loader_options options = {};
	int err;

	err = parse_args(argc, argv, &options);
	if (err < 0)
		return EXIT_FAILURE;
	if (err > 0)
		return EXIT_SUCCESS;

	libbpf_set_print(libbpf_log);

	signal(SIGINT, handle_signal);
	signal(SIGTERM, handle_signal);

	skel = netdisk_bpf_bpf__open();
	if (!skel) {
		fprintf(stderr, "failed to open BPF skeleton\n");
		return EXIT_FAILURE;
	}

	err = configure_programs(skel, &options);
	if (err) {
		fprintf(stderr, "failed to configure BPF programs: %d\n", err);
		goto cleanup;
	}

	err = netdisk_bpf_bpf__load(skel);
	if (err) {
		fprintf(stderr, "failed to load BPF skeleton: %d\n", err);
		goto cleanup;
	}

	err = netdisk_bpf_bpf__attach(skel);
	if (err) {
		fprintf(stderr, "failed to attach BPF skeleton: %d\n", err);
		goto cleanup;
	}

	ringbuf = ring_buffer__new(bpf_map__fd(skel->maps.events), handle_event,
				   NULL, NULL);
	if (!ringbuf) {
		err = errno ? -errno : -1;
		fprintf(stderr, "failed to create ring buffer: %d\n", err);
		goto cleanup;
	}

	fprintf(stderr,
		"netdisk loader started in %s mode (PID filter: %u); "
		"press Ctrl+C to stop\n",
		options.network ? "network" : "syscall", options.target_pid);

	while (!exiting) {
		err = ring_buffer__poll(ringbuf, 100);
		if (err == -EINTR) {
			if (exiting)
				break;
			continue;
		}
		if (err < 0) {
			fprintf(stderr, "ring buffer poll failed: %d\n", err);
			goto cleanup;
		}
	}

	err = 0;
	fprintf(stderr, "netdisk loader stopped\n");

cleanup:
	ring_buffer__free(ringbuf);
	netdisk_bpf_bpf__destroy(skel);

	return err < 0 ? -err : err;
}
