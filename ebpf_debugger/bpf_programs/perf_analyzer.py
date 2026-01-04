"""
Performance Analyzer - eBPF program for system performance monitoring

This module uses BCC to monitor:
- CPU usage per process
- Context switches
- CPU cycles sampling
"""

from bcc import BPF
import os

# eBPF程序源码
BPF_PROGRAM = """
#include <uapi/linux/ptrace.h>
#include <linux/sched.h>

// 上下文切换事件
struct switch_event_t {
    u32 prev_pid;
    u32 next_pid;
    char prev_comm[16];
    char next_comm[16];
    u64 timestamp;
};

// CPU采样事件
struct cpu_event_t {
    u32 pid;
    u32 cpu;
    char comm[16];
    u64 timestamp;
};

// Perf buffers
BPF_PERF_OUTPUT(switch_events);
BPF_PERF_OUTPUT(cpu_events);

// CPU时间统计
BPF_HASH(cpu_time, u32, u64);
BPF_HASH(start_time, u32, u64);

// 追踪上下文切换
TRACEPOINT_PROBE(sched, sched_switch) {
    struct switch_event_t event = {};

    event.prev_pid = args->prev_pid;
    event.next_pid = args->next_pid;
    event.timestamp = bpf_ktime_get_ns();

    bpf_probe_read_str(&event.prev_comm, sizeof(event.prev_comm), args->prev_comm);
    bpf_probe_read_str(&event.next_comm, sizeof(event.next_comm), args->next_comm);

    switch_events.perf_submit(args, &event, sizeof(event));

    // 更新CPU时间统计
    u32 prev_pid = args->prev_pid;
    u32 next_pid = args->next_pid;

    // 记录prev进程的CPU时间
    u64 *prev_start = start_time.lookup(&prev_pid);
    if (prev_start) {
        u64 delta = event.timestamp - *prev_start;
        u64 *total = cpu_time.lookup(&prev_pid);
        if (total) {
            *total += delta;
        } else {
            cpu_time.update(&prev_pid, &delta);
        }
        start_time.delete(&prev_pid);
    }

    // 记录next进程的开始时间
    start_time.update(&next_pid, &event.timestamp);

    return 0;
}

// CPU周期采样(使用software event)
int do_cpu_sample(struct bpf_perf_event_data *ctx) {
    struct cpu_event_t event = {};

    event.pid = bpf_get_current_pid_tgid() >> 32;
    event.cpu = bpf_get_smp_processor_id();
    event.timestamp = bpf_ktime_get_ns();
    bpf_get_current_comm(&event.comm, sizeof(event.comm));

    cpu_events.perf_submit(ctx, &event, sizeof(event));
    return 0;
}
"""


class PerfAnalyzer:
    """性能分析器"""

    def __init__(self, collector, target_pids=None):
        """
        初始化性能分析器

        Args:
            collector: PerfCollector实例
            target_pids: 要监控的PID列表(可选)
        """
        self.collector = collector
        self.target_pids = set(target_pids) if target_pids else None
        self.bpf = None
        self.running = False

    def start(self):
        """启动分析"""
        # 加载BPF程序
        self.bpf = BPF(text=BPF_PROGRAM)

        # 设置事件回调
        self.bpf["switch_events"].open_perf_buffer(self._handle_switch_event)
        self.bpf["cpu_events"].open_perf_buffer(self._handle_cpu_event)

        # 附加CPU采样(可选,需要特权)
        try:
            self.bpf.attach_perf_event(
                ev_type=1,  # PERF_TYPE_SOFTWARE
                ev_config=0,  # PERF_COUNT_SW_CPU_CLOCK
                fn_name="do_cpu_sample",
                sample_freq=10  # 降低到10Hz采样，减少样本丢失
            )
            print("[PerfAnalyzer] CPU sampling enabled")
        except Exception as e:
            print(f"[PerfAnalyzer] CPU sampling not available: {e}")

        self.running = True
        print("[PerfAnalyzer] Started")

    def poll(self, timeout=100):
        """轮询事件"""
        if self.bpf and self.running:
            self.bpf.perf_buffer_poll(timeout=timeout)

    def stop(self):
        """停止分析"""
        self.running = False
        if self.bpf:
            self.bpf.cleanup()
            self.bpf = None
        print("[PerfAnalyzer] Stopped")

    def _should_track(self, pid):
        """检查是否应该追踪此PID"""
        if self.target_pids is None:
            return True
        return pid in self.target_pids

    def _handle_switch_event(self, cpu, data, size):
        """处理上下文切换事件"""
        event = self.bpf["switch_events"].event(data)

        prev_pid = event.prev_pid
        next_pid = event.next_pid
        prev_comm = event.prev_comm.decode('utf-8', errors='replace')
        next_comm = event.next_comm.decode('utf-8', errors='replace')

        # 过滤
        if not self._should_track(prev_pid) and not self._should_track(next_pid):
            return

        self.collector.on_context_switch(
            prev_pid=prev_pid,
            prev_comm=prev_comm,
            next_pid=next_pid,
            next_comm=next_comm
        )

    def _handle_cpu_event(self, cpu, data, size):
        """处理CPU采样事件"""
        event = self.bpf["cpu_events"].event(data)

        pid = event.pid
        cpu_id = event.cpu
        comm = event.comm.decode('utf-8', errors='replace')

        if not self._should_track(pid):
            return

        # 简单计算CPU使用率(基于采样频率)
        self.collector.on_cpu_sample(
            cpu=cpu_id,
            pid=pid,
            comm=comm,
            usage=1.0  # 每次采样代表一个时间片
        )

    def get_cpu_time_stats(self):
        """获取CPU时间统计"""
        if not self.bpf:
            return {}

        stats = {}
        for pid, cpu_time in self.bpf["cpu_time"].items():
            stats[pid.value] = cpu_time.value
        return stats


# 测试代码
if __name__ == "__main__":
    import sys
    sys.path.insert(0, '..')
    from collectors.perf import PerfCollector

    collector = PerfCollector()
    analyzer = PerfAnalyzer(collector)

    try:
        analyzer.start()
        print("Analyzing performance... Press Ctrl+C to stop")
        while True:
            analyzer.poll()
    except KeyboardInterrupt:
        pass
    finally:
        analyzer.stop()
        print("\nStats:", collector.get_stats())
