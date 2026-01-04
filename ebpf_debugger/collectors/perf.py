"""Performance data collector - aggregates data from eBPF perf analyzer"""

import threading
import time
from collections import deque, defaultdict
from dataclasses import dataclass, field
from typing import Dict, List
import config


@dataclass
class FunctionStats:
    """函数统计"""
    call_count: int = 0
    total_time_ns: int = 0
    min_time_ns: int = float('inf')
    max_time_ns: int = 0


class PerfCollector:
    """性能数据收集器"""

    def __init__(self):
        self.lock = threading.Lock()

        # CPU使用率
        self.cpu_usage_history = deque(maxlen=config.HISTORY_SIZE)
        self.per_cpu_usage: Dict[int, float] = {}

        # 上下文切换
        self.context_switches = 0
        self.context_switch_history = deque(maxlen=config.HISTORY_SIZE)
        self._last_switch_count = 0

        # 函数统计(uprobe)
        self.function_stats: Dict[str, FunctionStats] = defaultdict(FunctionStats)

        # 调用链
        self.call_traces = deque(maxlen=100)

        # 最近事件
        self.recent_events = deque(maxlen=config.MAX_EVENTS)

    def on_cpu_sample(self, cpu: int, pid: int, comm: str, usage: float):
        """处理CPU采样事件"""
        with self.lock:
            self.per_cpu_usage[cpu] = usage

    def on_context_switch(self, prev_pid: int, prev_comm: str,
                          next_pid: int, next_comm: str):
        """处理上下文切换事件"""
        with self.lock:
            self.context_switches += 1

    def on_function_entry(self, pid: int, comm: str, func_name: str,
                          timestamp_ns: int):
        """处理函数入口事件(uprobe)"""
        with self.lock:
            self.recent_events.append({
                'type': 'func_entry',
                'time': time.time(),
                'timestamp_ns': timestamp_ns,
                'pid': pid,
                'comm': comm,
                'func': func_name
            })

    def on_function_return(self, pid: int, comm: str, func_name: str,
                           duration_ns: int, ret_value: int = 0):
        """处理函数返回事件(uretprobe)"""
        with self.lock:
            stats = self.function_stats[func_name]
            stats.call_count += 1
            stats.total_time_ns += duration_ns
            stats.min_time_ns = min(stats.min_time_ns, duration_ns)
            stats.max_time_ns = max(stats.max_time_ns, duration_ns)

            self.recent_events.append({
                'type': 'func_return',
                'time': time.time(),
                'pid': pid,
                'comm': comm,
                'func': func_name,
                'duration_us': duration_ns / 1000,
                'ret': ret_value
            })

    def on_call_trace(self, pid: int, comm: str, stack: List[str]):
        """处理调用栈采样"""
        with self.lock:
            self.call_traces.append({
                'time': time.time(),
                'pid': pid,
                'comm': comm,
                'stack': stack
            })

    def update_history(self):
        """更新历史数据(定期调用)"""
        with self.lock:
            # CPU使用率
            if self.per_cpu_usage:
                avg_usage = sum(self.per_cpu_usage.values()) / len(self.per_cpu_usage)
            else:
                avg_usage = 0

            self.cpu_usage_history.append({
                'time': time.time(),
                'usage': avg_usage,
                'per_cpu': dict(self.per_cpu_usage)
            })

            # 上下文切换速率
            switch_rate = self.context_switches - self._last_switch_count
            self._last_switch_count = self.context_switches

            self.context_switch_history.append({
                'time': time.time(),
                'rate': switch_rate
            })

    def get_stats(self) -> dict:
        """获取统计数据"""
        with self.lock:
            # 函数统计(按总时间排序)
            sorted_funcs = sorted(
                self.function_stats.items(),
                key=lambda x: x[1].total_time_ns,
                reverse=True
            )

            func_list = []
            for name, stats in sorted_funcs[:20]:
                avg_time = stats.total_time_ns / stats.call_count if stats.call_count > 0 else 0
                func_list.append({
                    'name': name,
                    'call_count': stats.call_count,
                    'total_time_ms': stats.total_time_ns / 1_000_000,
                    'avg_time_us': avg_time / 1000,
                    'min_time_us': stats.min_time_ns / 1000 if stats.min_time_ns != float('inf') else 0,
                    'max_time_us': stats.max_time_ns / 1000
                })

            return {
                'cpu_usage_history': list(self.cpu_usage_history),
                'context_switch_history': list(self.context_switch_history),
                'current_cpu_usage': dict(self.per_cpu_usage),
                'total_context_switches': self.context_switches,
                'functions': func_list,
                'call_traces': list(self.call_traces)[-10:],
                'recent_events': list(self.recent_events)[-20:]
            }
