"""服务器线程池、epoll 调度和文件协议的端到端集成测试。

每个测试都在独立临时目录中启动真实 ``server/server`` 进程，因此数据库和
``netdisk_data`` 不会污染仓库。测试通过 TCP 按线上协议直接构造请求，同时
观察进程、线程和文件系统结果。
"""

import concurrent.futures
import socket
import struct
import subprocess
import tempfile
import time
import unittest
from pathlib import Path


# 所有测试连接同一个本地端口；默认工作线程数用于验证线程数量保持固定。
REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
SERVER_BINARY = REPOSITORY_ROOT / "server" / "server"
SERVER_ADDRESS = ("127.0.0.1", 9000)
WORKER_COUNT = 4


def encode_field(value):
    """把字符串编码成服务器协议使用的“网络序长度 + UTF-8 内容”。"""
    encoded = value.encode("utf-8")
    return struct.pack("!I", len(encoded)) + encoded


class ThreadPoolServerTest(unittest.TestCase):
    """从客户端可观察行为验证并发模型、超时、安全边界和文件传输。"""

    def setUp(self):
        """为每个测试创建隔离工作目录并启动一份全新的服务器进程。"""
        if not SERVER_BINARY.is_file():
            self.skipTest("build server/server before running integration tests")

        self.temporary_directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary_directory.cleanup)
        self.process = None
        self._start_server(WORKER_COUNT, 128, 2)
        self.addCleanup(self._stop_server)

    def _start_server(self, workers, queue_capacity, timeout):
        """按给定并发参数启动服务器，并等待监听端口真正可连接。"""
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
        """在五秒期限内探测端口；进程提前退出时输出其 stdout/stderr。"""
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
        """优先发送 SIGTERM 正常停机，超时后才强制杀死测试进程。"""
        if self.process is None or self.process.poll() is not None:
            return
        self.process.terminate()
        try:
            self.process.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.communicate(timeout=5)

    def test_full_queue_rejects_connection_without_growing_workers(self):
        """单工作线程和单队列槽被占满时，新连接应被拒绝且服务仍可用。"""
        self._stop_server()
        self._start_server(workers=1, queue_capacity=1, timeout=5)

        first = socket.create_connection(SERVER_ADDRESS, timeout=2)
        first.sendall(b"V")
        time.sleep(0.1)  # Let the only worker block on the first connection.
        second = socket.create_connection(SERVER_ADDRESS, timeout=2)
        second.sendall(b"V")
        time.sleep(0.1)  # Let epoll dispatch the second connection to the queue.
        rejected = socket.create_connection(SERVER_ADDRESS, timeout=2)
        rejected.sendall(b"V")
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
        """发送 V 命令探测不存在的目录，作为服务器存活性检查。"""
        username = f"pool-probe-{index}"
        request = b"V" + encode_field(username) + encode_field("/")
        with socket.create_connection(SERVER_ADDRESS, timeout=2) as connection:
            connection.sendall(request)
            response = connection.recv(1)
        return response

    def test_fixed_workers_and_fifo_queue_handle_concurrent_clients(self):
        """大量并发及空闲连接不能动态增加线程数或阻塞正常请求。"""
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
            # Idle sockets stay in epoll and must not consume all workers.
            self.assertEqual(self._verify_missing_directory(2000), b"\x00")
        finally:
            for connection in idle_connections:
                connection.close()

        with concurrent.futures.ThreadPoolExecutor(max_workers=32) as executor:
            responses = list(executor.map(self._verify_missing_directory, range(100)))

        self.assertEqual(responses, [b"\x00"] * 100)
        self.assertEqual(len(list(task_directory.iterdir())), expected_threads)

    def test_epoll_tracks_and_expires_idle_connections(self):
        """确认进程创建 epoll fd，并会关闭超过首字节等待时限的空闲连接。"""
        fd_directory = Path(f"/proc/{self.process.pid}/fd")
        if not fd_directory.is_dir():
            self.skipTest("epoll fd assertion requires Linux procfs")

        fd_targets = []
        for descriptor in fd_directory.iterdir():
            try:
                fd_targets.append(descriptor.readlink().as_posix())
            except FileNotFoundError:
                continue
        self.assertIn("anon_inode:[eventpoll]", fd_targets)

        idle = socket.create_connection(SERVER_ADDRESS, timeout=2)
        idle.settimeout(4)
        try:
            self.assertEqual(idle.recv(1), b"")
        finally:
            idle.close()

        self.assertIsNone(self.process.poll())
        self.assertEqual(self._verify_missing_directory(3000), b"\x00")

    def test_invalid_or_truncated_fields_do_not_kill_workers(self):
        """畸形超长字段只能终止当前连接，不能导致工作线程或进程退出。"""
        with socket.create_connection(SERVER_ADDRESS, timeout=2) as connection:
            connection.sendall(b"R" + struct.pack("!I", 0xFFFFFFFF))
            self.assertEqual(connection.recv(1), b"")

        self.assertIsNone(self.process.poll())
        self.assertEqual(self._verify_missing_directory(999), b"\x00")

    def test_upload_protocol_writes_exact_file_contents(self):
        """上传包含 NUL 和非 UTF-8 字节的内容，验证服务器按声明长度原样落盘。"""
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
        """接近上限但合法的长路径应得到协议响应，而不是令工作线程崩溃。"""
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
        """恶意用户名和删除命令不得越过 netdisk_data 用户根目录。"""
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
        """SIGTERM 应唤醒正在等待半截协议字段的工作线程并干净退出。"""
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
    # 支持直接执行本文件，也支持由 ``python -m unittest discover`` 发现。
    unittest.main()
