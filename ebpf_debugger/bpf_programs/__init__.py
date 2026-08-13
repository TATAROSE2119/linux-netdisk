"""Monitor adapters, imported lazily during the incremental migration."""

from importlib import import_module


_MONITOR_MODULES = {
    "NetworkMonitor": ".network_monitor",
    "SyscallTracer": ".syscall_tracer",
    "PerfAnalyzer": ".perf_analyzer",
    "UprobeTracer": ".uprobe_tracer",
}

__all__ = list(_MONITOR_MODULES)


def __getattr__(name):
    """Load only the monitor requested by the caller."""
    try:
        module_name = _MONITOR_MODULES[name]
    except KeyError as error:
        raise AttributeError(f"module {__name__!r} has no attribute {name!r}") from error

    value = getattr(import_module(module_name, __name__), name)
    globals()[name] = value
    return value
