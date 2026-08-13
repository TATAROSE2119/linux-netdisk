
#include <asm-generic/errno-base.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

#include <bpf/libbpf.h>
#include <string.h>

#include "include/events.h"
#include "netdisk_bpf.skel.h"

static volatile sig_atomic_t exiting;

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

static int handle_event(void *ctx, void *data, size_t data_sz)
{
	const struct netdisk_event *event;

	(void)ctx;

	if (data_sz < sizeof(struct netdisk_event)) {
		fprintf(stderr,
			"received short event : got %zu bytes,expected %zu\n",
			data_sz, sizeof(struct netdisk_event));
		return 0;
	}

	event = data;
	// 任务1，3.。。

	// fprintf(stderr, "received unsupported event kind: %u\n ",
	// event->kind);

	if (event->kind != NETDISK_EVENT_NETWORK) {
		fprintf(stderr, "unsupported event kind: %u\n", event->kind);
		return 0;
	}

	const struct netdisk_network_event *network = &event->network;

	fprintf(stderr,
		"network action=%u pid=%u comm=%.*s sport=%u dport=%u\n",
		network->action, network->pid, 16, network->comm,
		network->sport, network->dport);

	return 0;
}

int main(void)
{
	struct netdisk_bpf_bpf *skel = NULL;
	struct ring_buffer *ringbuf = NULL;

	int err = 0;

	libbpf_set_print(libbpf_log);

	signal(SIGINT, handle_signal);
	signal(SIGTERM, handle_signal);

	skel = netdisk_bpf_bpf__open();
	if (!skel) {
		fprintf(stderr, "failed to open BPF skeleton\n");
		return 1;
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

	fprintf(stderr, "netdisk load started; press ctrl+C to stop \n");

	while (!exiting) {
		err = ring_buffer__poll(ringbuf, 100);

		if (err == -EINTR) {
			if (exiting) {
				break;
			}
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