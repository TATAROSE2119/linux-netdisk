import concurrent.futures
import socket
import struct
import subprocess
import tempfile
import time
import unittest
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
SERVER_BINARY = REPOSITORY_ROOT / "server" / "server"
SERVER_ADDRESS = ("127.0.0.1", 9000)
WORKER_COUNT = 4


def encode_field(value):
    encoded = value.encode("utf-8")
    return struct.pack("!I", len(encoded)) + encoded


class ThreadPoolServerTest(unittest.TestCase):
    def setUp(self):
        if not SERVER_BINARY.is_file():
            self.skipTest("build server/server before running integration tests")

        self.temporary_directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary_directory.cleanup)
        self.process = None
        self._start_server(WORKER_COUNT, 128, 2)
        self.addCleanup(self._stop_server)

    def _start_server(self, workers, queue_capacity, timeout):
        self.process = subprocess.Popen(
            [
                str(SERVER_BINARY),
                "--workers",
                str(workers),
                "--queue-capacity",
                str(queue_capacity),
                "--client-timeout",
                str(timeout),
            ],
            cwd=self.temporary_directory.name,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        self._wait_until_ready()

    def _wait_until_ready(self):
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                stdout, stderr = self.process.communicate()
                self.fail(
                    f"server exited with {self.process.returncode}\n"
                    f"stdout:\n{stdout}\nstderr:\n{stderr}"
                )
            try:
                with socket.create_connection(SERVER_ADDRESS, timeout=0.1):
                    return
            except OSError:
                time.sleep(0.02)
        self.fail("server did not start listening within five seconds")

    def _stop_server(self):
        if self.process is None or self.process.poll() is not None:
            return
        self.process.terminate()
        try:
            self.process.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.communicate(timeout=5)

    def test_full_queue_rejects_connection_without_growing_workers(self):
        self._stop_server()
        self._start_server(workers=1, queue_capacity=1, timeout=5)

        first = socket.create_connection(SERVER_ADDRESS, timeout=2)
        time.sleep(0.1)  # Let the only worker block on the first connection.
        second = socket.create_connection(SERVER_ADDRESS, timeout=2)
        time.sleep(0.1)  # Let accept() place the second connection in the queue.
        rejected = socket.create_connection(SERVER_ADDRESS, timeout=2)
        rejected.settimeout(2)

        try:
            try:
                response = rejected.recv(1)
            except ConnectionResetError:
                response = b""
            self.assertEqual(response, b"")
            self.assertIsNone(self.process.poll())
        finally:
            rejected.close()
            second.close()
            first.close()

        time.sleep(0.1)
        self.assertEqual(self._verify_missing_directory(1000), b"\x00")

    @staticmethod
    def _verify_missing_directory(index):
        username = f"pool-probe-{index}"
        request = b"V" + encode_field(username) + encode_field("/")
        with socket.create_connection(SERVER_ADDRESS, timeout=2) as connection:
            connection.sendall(request)
            response = connection.recv(1)
        return response

    def test_fixed_workers_and_fifo_queue_handle_concurrent_clients(self):
        task_directory = Path(f"/proc/{self.process.pid}/task")
        if not task_directory.is_dir():
            self.skipTest("thread-count assertion requires Linux procfs")

        expected_threads = WORKER_COUNT + 2  # main + signal waiter + workers
        self.assertEqual(len(list(task_directory.iterdir())), expected_threads)
        thread_names = [
            (task / "comm").read_text(encoding="utf-8").strip()
            for task in task_directory.iterdir()
        ]
        self.assertGreaterEqual(thread_names.count("server"), WORKER_COUNT + 1)

        idle_connections = [
            socket.create_connection(SERVER_ADDRESS, timeout=2) for _ in range(20)
        ]
        try:
            time.sleep(0.2)
            self.assertEqual(len(list(task_directory.iterdir())), expected_threads)
        finally:
            for connection in idle_connections:
                connection.close()

        with concurrent.futures.ThreadPoolExecutor(max_workers=32) as executor:
            responses = list(executor.map(self._verify_missing_directory, range(100)))

        self.assertEqual(responses, [b"\x00"] * 100)
        self.assertEqual(len(list(task_directory.iterdir())), expected_threads)

    def test_invalid_or_truncated_fields_do_not_kill_workers(self):
        with socket.create_connection(SERVER_ADDRESS, timeout=2) as connection:
            connection.sendall(b"R" + struct.pack("!I", 0xFFFFFFFF))
            self.assertEqual(connection.recv(1), b"")

        self.assertIsNone(self.process.poll())
        self.assertEqual(self._verify_missing_directory(999), b"\x00")

    def test_upload_protocol_writes_exact_file_contents(self):
        username = "upload-probe"
        filename = "payload.bin"
        payload = b"netdisk-thread-pool\x00\xff\n" * 257
        file_size = len(payload)
        request = (
            b"U"
            + encode_field(username)
            + encode_field("")
            + encode_field(filename)
            + struct.pack("!II", file_size >> 32, file_size & 0xFFFFFFFF)
            + payload
        )

        with socket.create_connection(SERVER_ADDRESS, timeout=2) as connection:
            connection.sendall(request)
            self.assertEqual(connection.recv(1), b"\x01")

        uploaded_file = (
            Path(self.temporary_directory.name)
            / "netdisk_data"
            / username
            / filename
        )
        self.assertEqual(uploaded_file.read_bytes(), payload)

    def test_long_valid_path_returns_protocol_response(self):
        request = (
            b"V"
            + encode_field("long-path-probe")
            + encode_field("a" * 600)
        )

        with socket.create_connection(SERVER_ADDRESS, timeout=2) as connection:
            connection.sendall(request)
            self.assertEqual(connection.recv(1), b"\x00")

        self.assertIsNone(self.process.poll())

    def test_delete_rejects_path_outside_user_root(self):
        server_root = Path(self.temporary_directory.name)
        (server_root / "netdisk_data").mkdir(exist_ok=True)
        sentinel = server_root / "outside-sentinel.txt"
        sentinel.write_bytes(b"must not be deleted")
        request = (
            b"X"
            + encode_field("..")
            + encode_field("/")
            + encode_field(sentinel.name)
        )

        with socket.create_connection(SERVER_ADDRESS, timeout=2) as connection:
            connection.sendall(request)
            self.assertEqual(connection.recv(1), b"\x00")

        self.assertEqual(sentinel.read_bytes(), b"must not be deleted")

    def test_shutdown_wakes_workers_blocked_on_clients(self):
        idle_connections = [
            socket.create_connection(SERVER_ADDRESS, timeout=2)
            for _ in range(WORKER_COUNT)
        ]
        try:
            for connection in idle_connections:
                connection.sendall(b"V\x00\x00")
            time.sleep(0.1)
            self.process.terminate()
            stdout, stderr = self.process.communicate(timeout=5)
        finally:
            for connection in idle_connections:
                connection.close()

        self.assertEqual(self.process.returncode, 0, stderr)
        self.assertIn("workers=4", stdout)
        self.assertIn("server stopped", stderr)


if __name__ == "__main__":
    unittest.main()
