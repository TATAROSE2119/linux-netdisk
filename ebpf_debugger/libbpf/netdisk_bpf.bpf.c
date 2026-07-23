#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
// #include <linux/bpf.h>
#include "include/event.h"

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 1<<24);
} events SEC(".maps");

char LICENSE[] SEC("license") = "Dual BSD/GPL";