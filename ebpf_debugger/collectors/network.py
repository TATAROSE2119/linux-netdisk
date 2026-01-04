"""Network data collector - aggregates data from eBPF network monitor"""

import threading
import time
from collections import deque
from dataclasses import dataclass, field
from typing import Dict, List, Optional
import config


@dataclass
class ConnectionInfo:
    """TCP连接信息"""
    pid: int
    comm: str
    saddr: str
    daddr: str
    sport: int
    dport: int
    bytes_sent: int = 0
    bytes_recv: int = 0
    start_time: float = field(default_factory=time.time)
    latency_samples: List[float] = field(default_factory=list)


class NetworkCollector:
    """网络数据收集器"""

    def __init__(self):
        self.lock = threading.Lock()
        self.connections: Dict[tuple, ConnectionInfo] = {}

        # 历史数据(用于图表)
        self.throughput_history = deque(maxlen=config.HISTORY_SIZE)
        self.connection_count_history = deque(maxlen=config.HISTORY_SIZE)
        self.latency_history = deque(maxlen=config.HISTORY_SIZE)

        # 统计数据
        self.total_bytes_sent = 0
        self.total_bytes_recv = 0
        self.total_connections = 0
        self.active_connections = 0

        # 最近事件
        self.recent_events = deque(maxlen=config.MAX_EVENTS)

    def on_connect(self, pid: int, comm: str, saddr: str, daddr: str,
                   sport: int, dport: int):
        """处理新连接事件"""
        key = (pid, saddr, daddr, sport, dport)
        with self.lock:
            self.connections[key] = ConnectionInfo(
                pid=pid, comm=comm, saddr=saddr, daddr=daddr,
                sport=sport, dport=dport
            )
            self.total_connections += 1
            self.active_connections = len(self.connections)
            self.recent_events.append({
                'type': 'connect',
                'time': time.time(),
                'pid': pid,
                'comm': comm,
                'src': f"{saddr}:{sport}",
                'dst': f"{daddr}:{dport}"
            })

    def on_close(self, pid: int, saddr: str, daddr: str, sport: int, dport: int):
        """处理连接关闭事件"""
        key = (pid, saddr, daddr, sport, dport)
        with self.lock:
            if key in self.connections:
                conn = self.connections.pop(key)
                self.active_connections = len(self.connections)
                self.recent_events.append({
                    'type': 'close',
                    'time': time.time(),
                    'pid': pid,
                    'comm': conn.comm,
                    'src': f"{saddr}:{sport}",
                    'dst': f"{daddr}:{dport}",
                    'bytes_sent': conn.bytes_sent,
                    'bytes_recv': conn.bytes_recv,
                    'duration': time.time() - conn.start_time
                })

    def on_send(self, pid: int, saddr: str, daddr: str, sport: int, dport: int,
                size: int):
        """处理数据发送事件"""
        key = (pid, saddr, daddr, sport, dport)
        with self.lock:
            if key in self.connections:
                self.connections[key].bytes_sent += size
            self.total_bytes_sent += size

    def on_recv(self, pid: int, saddr: str, daddr: str, sport: int, dport: int,
                size: int):
        """处理数据接收事件"""
        key = (pid, saddr, daddr, sport, dport)
        with self.lock:
            if key in self.connections:
                self.connections[key].bytes_recv += size
            self.total_bytes_recv += size

    def on_latency(self, pid: int, saddr: str, daddr: str, sport: int, dport: int,
                   latency_us: float):
        """处理延迟采样"""
        key = (pid, saddr, daddr, sport, dport)
        with self.lock:
            if key in self.connections:
                self.connections[key].latency_samples.append(latency_us)

    def update_history(self):
        """更新历史数据(定期调用)"""
        with self.lock:
            # 计算当前吞吐量
            self.throughput_history.append({
                'time': time.time(),
                'sent': self.total_bytes_sent,
                'recv': self.total_bytes_recv
            })

            # 连接数
            self.connection_count_history.append({
                'time': time.time(),
                'count': self.active_connections
            })

            # 平均延迟
            all_latencies = []
            for conn in self.connections.values():
                all_latencies.extend(conn.latency_samples)
                conn.latency_samples = []  # 清空已处理的采样

            avg_latency = sum(all_latencies) / len(all_latencies) if all_latencies else 0
            self.latency_history.append({
                'time': time.time(),
                'latency': avg_latency
            })

    def get_stats(self) -> dict:
        """获取统计数据"""
        with self.lock:
            connections_list = []
            for key, conn in self.connections.items():
                connections_list.append({
                    'pid': conn.pid,
                    'comm': conn.comm,
                    'src': f"{conn.saddr}:{conn.sport}",
                    'dst': f"{conn.daddr}:{conn.dport}",
                    'bytes_sent': conn.bytes_sent,
                    'bytes_recv': conn.bytes_recv,
                    'duration': time.time() - conn.start_time
                })

            return {
                'total_connections': self.total_connections,
                'active_connections': self.active_connections,
                'total_bytes_sent': self.total_bytes_sent,
                'total_bytes_recv': self.total_bytes_recv,
                'connections': connections_list,
                'throughput_history': list(self.throughput_history),
                'connection_history': list(self.connection_count_history),
                'latency_history': list(self.latency_history),
                'recent_events': list(self.recent_events)[-20:]  # 最近20个事件
            }
