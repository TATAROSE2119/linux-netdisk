"""Python adapter for syscall events emitted by the libbpf loader."""

import json

from ._loader_monitor import LoaderMonitor


TARGET_SYSCALLS = frozenset(
    {"read", "write", "openat", "close", "accept4", "sendto", "recvfrom"}
)


class SyscallTracer(LoaderMonitor):
    """Run the syscall loader and forward its JSON events to a collector."""

    _MODE = "syscall"

    def __init__(self, collector, pid_filter=None, comm_filter=None):
        super().__init__(collector, pid_filter=pid_filter)
        self.comm_filter = comm_filter

    def _handle_line(self, line):
        line = line.strip()
        if not line:
            return

        try:
            event = json.loads(line)
        except json.JSONDecodeError as error:
            self._logger.warning(
                "Ignoring invalid loader JSON: %s (%s)", line, error
            )
            return

        if not isinstance(event, dict):
            self._logger.warning("Ignoring non-object loader event: %r", event)
            return

        if event.get("type") != "syscall":
            return

        try:
            self._dispatch_event(event)
        except (KeyError, TypeError, ValueError) as error:
            self._logger.warning(
                "Ignoring malformed syscall event: %r (%s)", event, error
            )
        except Exception:
            self._logger.exception(
                "Syscall collector callback failed for event: %r", event
            )

    def _dispatch_event(self, event):
        syscall = event["syscall"]
        comm = event["comm"]
        if not isinstance(syscall, str) or not isinstance(comm, str):
            raise TypeError("syscall and comm must be strings")
        if syscall not in TARGET_SYSCALLS:
            return

        pid = int(event["pid"])
        tid = int(event["tid"])
        duration_ns = int(event["duration_ns"])
        ret = int(event["ret"])
        timestamp_ns = int(event["timestamp_ns"])

        if pid < 0 or tid < 0:
            raise ValueError("pid and tid cannot be negative")
        if duration_ns < 0 or timestamp_ns < 0:
            raise ValueError("timestamps and duration cannot be negative")

        if self.comm_filter and self.comm_filter not in comm:
            return

        self.collector.on_syscall(
            pid=pid,
            comm=comm,
            syscall=syscall,
            duration_ns=duration_ns,
            ret=ret,
        )
