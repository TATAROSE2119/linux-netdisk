import json
import stat
import subprocess
import sys
import tempfile
import textwrap
import threading
import unittest
from pathlib import Path


DEBUGGER_ROOT = Path(__file__).resolve().parents[1]
if str(DEBUGGER_ROOT) not in sys.path:
    sys.path.insert(0, str(DEBUGGER_ROOT))

from collectors.syscall import SyscallCollector
from ebpf_debugger.bpf_programs.syscall_tracer import SyscallTracer


class RecordingSyscallCollector:
    def __init__(self, expected_calls=0):
        self.calls = []
        self._expected_calls = expected_calls
        self._lock = threading.Lock()
        self._complete = threading.Event()

    def on_syscall(self, **event):
        with self._lock:
            self.calls.append(event)
            if len(self.calls) >= self._expected_calls:
                self._complete.set()

    def wait(self, timeout=3):
        return self._complete.wait(timeout)


class SyscallTracerTest(unittest.TestCase):
    def setUp(self):
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary_directory.cleanup)
        self.directory = Path(self.temporary_directory.name)

    def _write_loader(self, source):
        loader_path = self.directory / "fake_loader"
        loader_path.write_text(
            textwrap.dedent(source).lstrip(),
            encoding="utf-8",
        )
        loader_path.chmod(loader_path.stat().st_mode | stat.S_IXUSR)
        return loader_path

    def test_loader_events_are_dispatched_with_filters(self):
        loader_path = self._write_loader(
            r'''
            #!/usr/bin/env python3
            import json
            import sys
            import time

            if sys.argv[1:] != ["--syscall", "--pid", "4321"]:
                raise SystemExit(64)

            def emit(event):
                print(json.dumps(event), flush=True)

            print("this is not json", flush=True)
            emit(["not", "an", "object"])
            emit({"type": "network", "action": 1})
            emit({
                "type": "syscall", "pid": 4321, "tid": 4322,
                "comm": "server-worker", "syscall": "ioctl",
                "duration_ns": 1, "ret": 0, "timestamp_ns": 20,
            })
            emit({
                "type": "syscall", "pid": 4321, "tid": 4322,
                "comm": "server-worker", "syscall": "read",
                "duration_ns": -1, "ret": 0, "timestamp_ns": 21,
            })
            emit({
                "type": "syscall", "pid": 4321, "tid": 4322,
                "comm": "server-worker", "syscall": "read",
                "duration_ns": 1, "timestamp_ns": 21,
            })
            emit({
                "type": "syscall", "pid": 4321, "tid": 4322,
                "comm": "client", "syscall": "read",
                "duration_ns": 1, "ret": 1, "timestamp_ns": 22,
            })

            syscalls = [
                ("read", 1000, 128),
                ("write", 2000, 64),
                ("openat", 3000, -2),
                ("close", 4000, 0),
                ("accept4", 5000, 7),
                ("sendto", 6000, 32),
                ("recvfrom", 7000, 16),
            ]
            for timestamp, (name, duration, result) in enumerate(syscalls, 10):
                emit({
                    "type": "syscall", "pid": 4321, "tid": 4322,
                    "comm": "server-worker", "syscall": name,
                    "duration_ns": duration, "ret": result,
                    "timestamp_ns": timestamp,
                })
            time.sleep(60)
            '''
        )
        collector = RecordingSyscallCollector(expected_calls=7)
        tracer = SyscallTracer(
            collector,
            pid_filter=4321,
            comm_filter="server",
        )
        tracer.loader_path = loader_path

        process = None
        reader_thread = None
        try:
            with self.assertLogs(
                "ebpf_debugger.bpf_programs.syscall_tracer", level="WARNING"
            ) as captured_logs:
                tracer.start()
                process = tracer.process
                reader_thread = tracer.reader_thread
                tracer.start()
                self.assertIs(tracer.process, process)
                self.assertIsNone(tracer.poll())
                self.assertTrue(collector.wait(), "fake syscall events were not read")

            log_output = "\n".join(captured_logs.output)
            self.assertIn("Ignoring invalid loader JSON", log_output)
            self.assertIn("Ignoring non-object loader event", log_output)
            self.assertIn("Ignoring malformed syscall event", log_output)

            self.assertEqual(
                [event["syscall"] for event in collector.calls],
                [
                    "read",
                    "write",
                    "openat",
                    "close",
                    "accept4",
                    "sendto",
                    "recvfrom",
                ],
            )
            self.assertEqual(collector.calls[0]["duration_ns"], 1000)
            self.assertEqual(collector.calls[0]["ret"], 128)
            self.assertEqual(collector.calls[2]["ret"], -2)
            self.assertTrue(all(event["pid"] == 4321 for event in collector.calls))
        finally:
            tracer.stop()
            tracer.stop()

        self.assertIsNotNone(process)
        self.assertIsNotNone(process.poll())
        self.assertIsNotNone(reader_thread)
        self.assertFalse(reader_thread.is_alive())

    def test_collector_updates_count_average_and_errors(self):
        collector = SyscallCollector()
        tracer = SyscallTracer(collector)

        base_event = {
            "type": "syscall",
            "pid": 1234,
            "tid": 1234,
            "comm": "server",
            "syscall": "read",
            "timestamp_ns": 100,
        }
        tracer._handle_line(
            json.dumps({**base_event, "duration_ns": 1000, "ret": 128})
        )
        tracer._handle_line(
            json.dumps({**base_event, "duration_ns": 3000, "ret": -9})
        )

        stats = collector.get_stats()
        read_stats = next(item for item in stats["syscalls"] if item["name"] == "read")
        self.assertEqual(read_stats["count"], 2)
        self.assertEqual(read_stats["avg_time_us"], 2.0)
        self.assertEqual(read_stats["errors"], 1)
        self.assertEqual(read_stats["error_rate"], 50.0)

    def test_syscall_tracer_import_does_not_require_bcc(self):
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
from ebpf_debugger.bpf_programs.syscall_tracer import SyscallTracer
from ebpf_debugger.bpf_programs import SyscallTracer as ExportedSyscallTracer
assert SyscallTracer is ExportedSyscallTracer
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
