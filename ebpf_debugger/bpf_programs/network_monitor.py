"""Python adapter for network events emitted by the libbpf loader."""

import json

from ._loader_monitor import LoaderMonitor


EVENT_CONNECT = 1
EVENT_ACCEPT = 2
EVENT_CLOSE = 3
EVENT_SEND = 4
EVENT_RECV = 5


class NetworkMonitor(LoaderMonitor):
    """Run the network loader and forward its JSON events to a collector."""

    _MODE = "network"

    def __init__(self, collector, pid_filter=None):
        super().__init__(collector, pid_filter=pid_filter)

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

        if event.get("type") != "network":
            return

        try:
            self._dispatch_event(event)
        except (KeyError, TypeError, ValueError) as error:
            self._logger.warning(
                "Ignoring malformed network event: %r (%s)", event, error
            )
        except Exception:
            self._logger.exception(
                "Network collector callback failed for event: %r", event
            )

    def _dispatch_event(self, event):
        action = int(event["action"])
        if action not in {
            EVENT_CONNECT,
            EVENT_ACCEPT,
            EVENT_CLOSE,
            EVENT_SEND,
            EVENT_RECV,
        }:
            return

        pid = int(event["pid"])
        source_address = str(event["saddr"])
        destination_address = str(event["daddr"])
        source_port = int(event["sport"])
        destination_port = int(event["dport"])

        if pid < 0:
            raise ValueError("pid cannot be negative")
        if not 0 <= source_port <= 65535 or not 0 <= destination_port <= 65535:
            raise ValueError("network port is outside the valid range")

        if action in {EVENT_CONNECT, EVENT_ACCEPT}:
            self.collector.on_connect(
                pid,
                str(event["comm"]),
                source_address,
                destination_address,
                source_port,
                destination_port,
            )
        elif action == EVENT_CLOSE:
            self.collector.on_close(
                pid,
                source_address,
                destination_address,
                source_port,
                destination_port,
            )
        elif action == EVENT_SEND:
            byte_count = int(event["bytes"])
            if byte_count < 0:
                raise ValueError("byte count cannot be negative")
            self.collector.on_send(
                pid,
                source_address,
                destination_address,
                source_port,
                destination_port,
                byte_count,
            )
        elif action == EVENT_RECV:
            byte_count = int(event["bytes"])
            if byte_count < 0:
                raise ValueError("byte count cannot be negative")
            self.collector.on_recv(
                pid,
                source_address,
                destination_address,
                source_port,
                destination_port,
                byte_count,
            )
