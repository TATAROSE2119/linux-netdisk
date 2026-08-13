#ifndef NETDISK_EVENTS_H
#define NETDISK_EVENTS_H

#ifndef __VMLINUX_H__
#include <linux/types.h>
#endif

enum netdisk_event_kind {
	NETDISK_EVENT_NETWORK = 1,
	NETDISK_EVENT_SYSCALL = 2,
	NETDISK_EVENT_PERF = 3,
	NETDISK_EVENT_UPROBE = 4,
};

enum netdisk_network_action {
	NETDISK_NET_CONNECT = 1,
	NETDISK_NET_ACCEPT = 2,
	NETDISK_NET_CLOSE = 3,
	NETDISK_NET_SEND = 4,
	NETDISK_NET_RECV = 5,
};

enum netdisk_syscall_id {
	NETDISK_SYSCALL_READ = 1,
	NETDISK_SYSCALL_WRITE = 2,
	NETDISK_SYSCALL_OPENAT = 3,
	NETDISK_SYSCALL_CLOSE = 4,
	NETDISK_SYSCALL_ACCEPT4 = 5,
	NETDISK_SYSCALL_SENDTO = 6,
	NETDISK_SYSCALL_RECVFROM = 7,
};

struct netdisk_network_event {
	__u32 pid;
	__u32 tid;
	__u64 timestamp_ns;
	__u64 bytes;

	__u32 saddr;
	__u32 daddr;
	__u16 sport;
	__u16 dport;

	__u8 action;
	__u8 reserved[3];

	char comm[16];
};

struct netdisk_syscall_event {
	__u32 pid;
	__u32 tid;
	__u64 timestamp_ns;
	__u64 duration_ns;
	__s64 ret;

	__u8 syscall_id;
	__u8 reserved[7];

	char comm[16];
};

struct netdisk_event {
	__u32 kind;
	__u32 reserved;

	union {
		struct netdisk_network_event network;
		struct netdisk_syscall_event syscall;
	};
};

#endif
