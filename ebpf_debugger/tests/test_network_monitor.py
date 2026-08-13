import signal
import stat
import subprocess
import sys
import tempfile
import textwrap
import threading
import unittest
from pathlib import Path

from ebpf_debugger.bpf_programs.network_monitor import NetworkMonitor


class RecordingCollector:
    def __init__(self, expected_calls=0):
        self.calls = []
        self._expected_calls = expected_calls
        self._lock = threading.Lock()
        self._complete = threading.Event()

    def _record(self, name, *arguments):
        with self._lock:
            self.calls.append((name, arguments))
            if len(self.calls) >= self._expected_calls:
                self._complete.set()

    def on_connect(self, *arguments):
        self._record("connect", *arguments)

    def on_close(self, *arguments):
        self._record("close", *arguments)

    def on_send(self, *arguments):
        self._record("send", *arguments)

    def on_recv(self, *arguments):
        self._record("recv", *arguments)

    def wait(self, timeout=3):
        return self._complete.wait(timeout)


class NetworkMonitorTest(unittest.TestCase):
    def setUp(self):
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary_directory.cleanup)
        self.directory = Path(self.temporary_directory.name)

    def _write_loader(self, source, executable=True):
        loader_path = self.directory / "fake_loader"
        loader_path.write_text(
            textwrap.dedent(source).lstrip(),
            encoding="utf-8",
        )
        mode = loader_path.stat().st_mode
        if executable:
            loader_path.chmod(mode | stat.S_IXUSR)
        else:
            loader_path.chmod(mode & ~(stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH))
        return loader_path

    def test_loader_events_are_dispatched_and_bad_lines_are_ignored(self):
        loader_path = self._write_loader(
            r'''
            #!/usr/bin/env python3
            import json
            import sys
            import time

            if sys.argv[1:] != ["--network", "--pid", "4321"]:
                raise SystemExit(64)

            def emit(event):
                print(json.dumps(event), flush=True)

            emit({
                "type": "network", "action": 1, "pid": 101, "tid": 101,
                "comm": "client", "saddr": "127.0.0.1",
                "daddr": "127.0.0.1", "sport": 51000, "dport": 8080,
                "bytes": 0, "timestamp_ns": 1,
            })
            print("this is not json", flush=True)
            emit({
                "type": "network", "action": 2, "pid": 202, "tid": 202,
                "comm": "server", "saddr": "127.0.0.1",
                "daddr": "127.0.0.1", "sport": 8080, "dport": 51000,
                "bytes": 0, "timestamp_ns": 2,
            })
            emit({
                "type": "network", "action": 4, "pid": 101, "tid": 101,
                "comm": "client", "saddr": "127.0.0.1",
                "daddr": "127.0.0.1", "sport": 51000, "dport": 8080,
                "bytes": 128, "timestamp_ns": 3,
            })
            emit({"type": "syscall", "event": "read"})
            emit({"type": "network", "action": 99})
            emit({
                "type": "network", "action": 4, "pid": 101,
                "saddr": "127.0.0.1", "daddr": "127.0.0.1",
                "sport": 51000, "dport": 8080,
            })
            emit({
                "type": "network", "action": 4, "pid": 101, "tid": 101,
                "comm": "client", "saddr": "127.0.0.1",
                "daddr": "127.0.0.1", "sport": 51000, "dport": 8080,
                "bytes": -1, "timestamp_ns": 4,
            })
            emit({
                "type": "network", "action": 5, "pid": 101, "tid": 101,
                "comm": "client", "saddr": "127.0.0.1",
                "daddr": "127.0.0.1", "sport": 51000, "dport": 8080,
                "bytes": 256, "timestamp_ns": 4,
            })
            emit({
                "type": "network", "action": 3, "pid": 101, "tid": 101,
                "comm": "client", "saddr": "127.0.0.1",
                "daddr": "127.0.0.1", "sport": 51000, "dport": 8080,
                "bytes": 0, "timestamp_ns": 5,
            })
            time.sleep(60)
            '''
        )
        collector = RecordingCollector(expected_calls=5)
        monitor = NetworkMonitor(collector, pid_filter=4321)
        monitor.loader_path = loader_path

        process = None
        reader_thread = None
        try:
            with self.assertLogs(
                "ebpf_debugger.bpf_programs.network_monitor", level="WARNING"
            ) as captured_logs:
                monitor.start()
                process = monitor.process
                reader_thread = monitor.reader_thread

                monitor.start()
                self.assertIs(monitor.process, process)
                self.assertIsNone(monitor.poll())
                self.assertTrue(collector.wait(), "fake loader events were not consumed")

            log_output = "\n".join(captured_logs.output)
            self.assertIn("Ignoring invalid loader JSON", log_output)
            self.assertIn("Ignoring malformed network event", log_output)

            self.assertEqual(
                collector.calls,
                [
                    (
                        "connect",
                        (101, "client", "127.0.0.1", "127.0.0.1", 51000, 8080),
                    ),
                    (
                        "connect",
                        (202, "server", "127.0.0.1", "127.0.0.1", 8080, 51000),
                    ),
                    (
                        "send",
                        (101, "127.0.0.1", "127.0.0.1", 51000, 8080, 128),
                    ),
                    (
                        "recv",
                        (101, "127.0.0.1", "127.0.0.1", 51000, 8080, 256),
                    ),
                    (
                        "close",
                        (101, "127.0.0.1", "127.0.0.1", 51000, 8080),
                    ),
                ],
            )
        finally:
            monitor.stop()
            monitor.stop()

        self.assertIsNotNone(process)
        self.assertIsNotNone(process.poll())
        self.assertIsNotNone(reader_thread)
        self.assertFalse(reader_thread.is_alive())
        self.assertIsNone(monitor.process)
        self.assertIsNone(monitor.reader_thread)
        self.assertFalse(monitor.running)

    def test_poll_reports_an_unexpected_loader_exit(self):
        loader_path = self._write_loader(
            '''
            #!/usr/bin/env python3
            raise SystemExit(7)
            '''
        )
        monitor = NetworkMonitor(RecordingCollector())
        monitor.loader_path = loader_path

        try:
            monitor.start()
            monitor.process.wait(timeout=3)
            with self.assertRaisesRegex(RuntimeError, "status 7"):
                monitor.poll()
            self.assertFalse(monitor.running)
        finally:
            monitor.stop()

    def test_stop_kills_a_loader_that_ignores_terminate(self):
        loader_path = self._write_loader(
            r'''
            #!/usr/bin/env python3
            import json
            import signal
            import time

            signal.signal(signal.SIGTERM, signal.SIG_IGN)
            print(json.dumps({
                "type": "network", "action": 1, "pid": 1, "tid": 1,
                "comm": "ready", "saddr": "127.0.0.1",
                "daddr": "127.0.0.1", "sport": 50000, "dport": 8080,
                "bytes": 0, "timestamp_ns": 1,
            }), flush=True)
            while True:
                time.sleep(1)
            '''
        )
        collector = RecordingCollector(expected_calls=1)
        monitor = NetworkMonitor(collector)
        monitor.loader_path = loader_path
        monitor._STOP_TIMEOUT_SECONDS = 0.05

        monitor.start()
        process = monitor.process
        self.assertTrue(collector.wait(), "fake loader did not become ready")

        with self.assertLogs(
            "ebpf_debugger.bpf_programs.network_monitor", level="WARNING"
        ) as captured_logs:
            monitor.stop()

        self.assertIn(
            "Network loader did not terminate; killing it",
            "\n".join(captured_logs.output),
        )
        self.assertEqual(process.returncode, -signal.SIGKILL)
        self.assertIsNone(monitor.process)

    def test_start_rejects_missing_or_non_executable_loader(self):
        collector = RecordingCollector()

        missing_monitor = NetworkMonitor(collector)
        missing_monitor.loader_path = self.directory / "missing_loader"
        with self.assertRaises(FileNotFoundError):
            missing_monitor.start()

        loader_path = self._write_loader("#!/bin/sh\nexit 0\n", executable=False)
        non_executable_monitor = NetworkMonitor(collector)
        non_executable_monitor.loader_path = loader_path
        with self.assertRaises(PermissionError):
            non_executable_monitor.start()

    def test_start_rejects_invalid_pid_filters(self):
        loader_path = self._write_loader("#!/bin/sh\nsleep 60\n")

        for pid_filter in (0, -1, "abc", 1.5, True, 0x100000000):
            with self.subTest(pid_filter=pid_filter):
                monitor = NetworkMonitor(RecordingCollector(), pid_filter=pid_filter)
                monitor.loader_path = loader_path
                with self.assertRaises(ValueError):
                    monitor.start()
                self.assertIsNone(monitor.process)

    def test_network_monitor_import_does_not_require_bcc(self):
        repository_root = Path(__file__).resolve().parents[2]
        source = r'''
import importlib.abc
import sys

class BlockBcc(importlib.abc.MetaPathFinder):
    def find_spec(self, fullname, path, target=None):
        if fullname == "bcc" or fullname.startswith("bcc."):
            raise ModuleNotFoundError("bcc intentionally unavailable")
        return None

sys.meta_path.insert(0, BlockBcc())
from ebpf_debugger.bpf_programs.network_monitor import NetworkMonitor
from ebpf_debugger.bpf_programs import NetworkMonitor as ExportedNetworkMonitor
assert NetworkMonitor is ExportedNetworkMonitor
'''
        result = subprocess.run(
            [sys.executable, "-c", textwrap.dedent(source)],
            cwd=repository_root,
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
