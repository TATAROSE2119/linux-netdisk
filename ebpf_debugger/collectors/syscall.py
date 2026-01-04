"""Syscall data collector - aggregates data from eBPF syscall tracer"""

import threading
import time
from collections import deque, defaultdict
from dataclasses import dataclass, field
from typing import Dict, List
import config


@dataclass
class SyscallStats:
    """系统调用统计"""
    count: int = 0
    total_time_ns: int = 0
    errors: int = 0
    last_args: List[str] = field(default_factory=list)


class SyscallCollector:
    """系统调用数据收集器"""

    SYSCALL_NAMES = {
        # 文件操作
        'read': '读取文件',
        'write': '写入文件',
        'open': '打开文件',
        'openat': '打开文件(at)',
        'close': '关闭文件',
        'stat': '获取文件状态',
        'fstat': '获取文件状态(fd)',
        'lstat': '获取链接状态',
        'unlink': '删除文件',
        'rename': '重命名文件',
        'mkdir': '创建目录',
        'rmdir': '删除目录',
        # Socket操作
        'socket': '创建Socket',
        'connect': '连接',
        'accept': '接受连接',
        'accept4': '接受连接4',
        'bind': '绑定地址',
        'listen': '监听',
        'sendto': '发送数据',
        'recvfrom': '接收数据',
        'sendmsg': '发送消息',
        'recvmsg': '接收消息',
        'shutdown': '关闭连接',
        # 进程操作
        'clone': '克隆进程',
        'fork': '创建子进程',
        'execve': '执行程序',
        'exit': '退出',
        'wait4': '等待子进程',
    }

    def __init__(self):
        self.lock = threading.Lock()

        # 按系统调用类型统计
        self.syscall_stats: Dict[str, SyscallStats] = defaultdict(SyscallStats)

        # 按进程统计
        self.process_stats: Dict[int, Dict[str, SyscallStats]] = defaultdict(
            lambda: defaultdict(SyscallStats)
        )

        # 文件I/O统计
        self.file_io_stats: Dict[str, dict] = {}

        # 历史数据
        self.syscall_rate_history = deque(maxlen=config.HISTORY_SIZE)
        self.io_rate_history = deque(maxlen=config.HISTORY_SIZE)

        # 最近事件
        self.recent_events = deque(maxlen=config.MAX_EVENTS)

        # 计数器(用于计算速率)
        self._last_total_count = 0
        self._last_io_bytes = 0

    def on_syscall(self, pid: int, comm: str, syscall: str,
                   duration_ns: int, ret: int, args: List[str] = None):
        """处理系统调用事件"""
        with self.lock:
            # 更新全局统计
            stats = self.syscall_stats[syscall]
            stats.count += 1
            stats.total_time_ns += duration_ns
            if ret < 0:
                stats.errors += 1
            if args:
                stats.last_args = args[:3]  # 保存最近的参数

            # 更新进程统计
            pstats = self.process_stats[pid][syscall]
            pstats.count += 1
            pstats.total_time_ns += duration_ns
            if ret < 0:
                pstats.errors += 1

            # 记录事件
            self.recent_events.append({
                'time': time.time(),
                'pid': pid,
                'comm': comm,
                'syscall': syscall,
                'syscall_cn': self.SYSCALL_NAMES.get(syscall, syscall),
                'duration_us': duration_ns / 1000,
                'ret': ret,
                'args': args or []
            })

    def on_file_io(self, pid: int, comm: str, fd: int, filename: str,
                   op: str, size: int, duration_ns: int):
        """处理文件I/O事件"""
        with self.lock:
            if filename not in self.file_io_stats:
                self.file_io_stats[filename] = {
                    'read_bytes': 0,
                    'write_bytes': 0,
                    'read_count': 0,
                    'write_count': 0,
                    'total_time_ns': 0,
                    'last_pid': pid,
                    'last_comm': comm
                }

            stats = self.file_io_stats[filename]
            stats['total_time_ns'] += duration_ns
            stats['last_pid'] = pid
            stats['last_comm'] = comm

            if op == 'read':
                stats['read_bytes'] += size
                stats['read_count'] += 1
            else:
                stats['write_bytes'] += size
                stats['write_count'] += 1

    def update_history(self):
        """更新历史数据(定期调用)"""
        with self.lock:
            # 计算系统调用速率
            total_count = sum(s.count for s in self.syscall_stats.values())
            rate = total_count - self._last_total_count
            self._last_total_count = total_count

            self.syscall_rate_history.append({
                'time': time.time(),
                'rate': rate
            })

            # 计算I/O速率
            total_io = sum(
                s['read_bytes'] + s['write_bytes']
                for s in self.file_io_stats.values()
            )
            io_rate = total_io - self._last_io_bytes
            self._last_io_bytes = total_io

            self.io_rate_history.append({
                'time': time.time(),
                'rate': io_rate
            })

    def get_stats(self) -> dict:
        """获取统计数据"""
        with self.lock:
            # 按调用次数排序的系统调用
            sorted_syscalls = sorted(
                self.syscall_stats.items(),
                key=lambda x: x[1].count,
                reverse=True
            )

            syscall_list = []
            for name, stats in sorted_syscalls[:20]:
                avg_time = stats.total_time_ns / stats.count if stats.count > 0 else 0
                syscall_list.append({
                    'name': name,
                    'name_cn': self.SYSCALL_NAMES.get(name, name),
                    'count': stats.count,
                    'avg_time_us': avg_time / 1000,
                    'errors': stats.errors,
                    'error_rate': stats.errors / stats.count * 100 if stats.count > 0 else 0
                })

            # 文件I/O统计
            sorted_files = sorted(
                self.file_io_stats.items(),
                key=lambda x: x[1]['read_bytes'] + x[1]['write_bytes'],
                reverse=True
            )

            file_io_list = []
            for filename, stats in sorted_files[:20]:
                file_io_list.append({
                    'filename': filename,
                    'read_bytes': stats['read_bytes'],
                    'write_bytes': stats['write_bytes'],
                    'read_count': stats['read_count'],
                    'write_count': stats['write_count'],
                    'last_pid': stats['last_pid'],
                    'last_comm': stats['last_comm']
                })

            # 进程统计
            process_list = []
            for pid, syscalls in self.process_stats.items():
                total = sum(s.count for s in syscalls.values())
                process_list.append({
                    'pid': pid,
                    'total_syscalls': total,
                    'top_syscalls': sorted(
                        [(k, v.count) for k, v in syscalls.items()],
                        key=lambda x: x[1],
                        reverse=True
                    )[:5]
                })

            process_list.sort(key=lambda x: x['total_syscalls'], reverse=True)

            return {
                'syscalls': syscall_list,
                'file_io': file_io_list,
                'processes': process_list[:10],
                'syscall_rate_history': list(self.syscall_rate_history),
                'io_rate_history': list(self.io_rate_history),
                'recent_events': list(self.recent_events)[-30:]
            }
