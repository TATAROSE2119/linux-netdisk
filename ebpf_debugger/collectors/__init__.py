"""Data collectors for eBPF debugger"""

from .network import NetworkCollector
from .syscall import SyscallCollector
from .perf import PerfCollector

__all__ = ['NetworkCollector', 'SyscallCollector', 'PerfCollector']
