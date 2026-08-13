"""Shared subprocess lifecycle for JSON-lines libbpf monitor adapters."""

import logging
import os
import subprocess
import threading
from pathlib import Path


class LoaderMonitor:
    """Run one netdisk loader mode and consume its stdout in a daemon thread."""

    _MODE = None
    _STOP_TIMEOUT_SECONDS = 3

    def __init__(self, collector, pid_filter=None):
        self.collector = collector
        self.pid_filter = pid_filter
        self.loader_path = (
            Path(__file__).resolve().parent.parent / "libbpf" / "netdisk_loader"
        )

        self.process = None
        self.reader_thread = None
        self.running = False
        self._lifecycle_lock = threading.RLock()
        self._logger = logging.getLogger(self.__class__.__module__)

    def start(self):
        """Start the loader and its stdout reader thread."""
        with self._lifecycle_lock:
            if self.process is not None:
                if self.process.poll() is None:
                    return
                self.stop()

            loader_path = Path(self.loader_path)
            if not loader_path.is_file():
                raise FileNotFoundError(f"libbpf loader not found: {loader_path}")
            if not os.access(loader_path, os.X_OK):
                raise PermissionError(
                    f"libbpf loader is not executable: {loader_path}"
                )

            process = subprocess.Popen(
                self._build_command(loader_path),
                stdout=subprocess.PIPE,
                stderr=None,
                text=True,
                encoding="utf-8",
                errors="replace",
                bufsize=1,
            )
            reader_thread = threading.Thread(
                target=self._read_stdout,
                args=(process,),
                name=f"{self._MODE}-loader-reader",
                daemon=True,
            )

            self.process = process
            self.reader_thread = reader_thread
            self.running = True

            try:
                reader_thread.start()
            except Exception:
                self.running = False
                self.process = None
                self.reader_thread = None
                self._terminate_process(process)
                raise

            self._logger.info("%s loader started with PID %s", self._MODE, process.pid)

    def poll(self, timeout=100):
        """Keep the legacy polling API and report an unexpected loader exit."""
        del timeout

        with self._lifecycle_lock:
            process = self.process
            if process is None or not self.running:
                return

            return_code = process.poll()
            if return_code is not None:
                self.running = False
                raise RuntimeError(
                    f"{self._MODE} loader exited unexpectedly "
                    f"with status {return_code}"
                )

    def stop(self):
        """Stop and reap the loader; calling this repeatedly is safe."""
        with self._lifecycle_lock:
            process = self.process
            reader_thread = self.reader_thread
            self.running = False

            if process is None:
                self.reader_thread = None
                return

            self._terminate_process(process)

            if (
                reader_thread is not None
                and reader_thread is not threading.current_thread()
            ):
                reader_thread.join(timeout=self._STOP_TIMEOUT_SECONDS)
                if reader_thread.is_alive():
                    self._logger.warning(
                        "%s loader reader thread did not stop in time", self._MODE
                    )

            if process.stdout is not None and not process.stdout.closed:
                process.stdout.close()

            self.process = None
            self.reader_thread = None
            self._logger.info("%s loader stopped", self._MODE)

    def _build_command(self, loader_path):
        if not self._MODE:
            raise RuntimeError("loader mode is not configured")

        command = [str(loader_path), f"--{self._MODE}"]
        if self.pid_filter is not None:
            pid = self._validate_pid_filter(self.pid_filter)
            command.extend(["--pid", str(pid)])
        return command

    @staticmethod
    def _validate_pid_filter(pid_filter):
        if isinstance(pid_filter, bool):
            raise ValueError("pid_filter must be a positive integer")

        try:
            pid = int(pid_filter)
        except (TypeError, ValueError) as error:
            raise ValueError("pid_filter must be a positive integer") from error

        if pid <= 0 or pid > 0xFFFFFFFF or str(pid) != str(pid_filter).strip():
            raise ValueError("pid_filter must be a positive integer")

        return pid

    def _terminate_process(self, process):
        if process.poll() is None:
            try:
                process.terminate()
            except ProcessLookupError:
                pass

            try:
                process.wait(timeout=self._STOP_TIMEOUT_SECONDS)
            except subprocess.TimeoutExpired:
                self._logger.warning(
                    "%s loader did not terminate; killing it",
                    self._MODE.capitalize(),
                )
                try:
                    process.kill()
                except ProcessLookupError:
                    pass
                process.wait()
        else:
            process.wait()

    def _read_stdout(self, process):
        stdout = process.stdout
        if stdout is None:
            self._logger.error("%s loader stdout pipe is unavailable", self._MODE)
            return

        try:
            for line in stdout:
                self._handle_line(line)
        except (OSError, ValueError) as error:
            if self.running:
                self._logger.warning(
                    "Failed while reading %s loader output: %s", self._MODE, error
                )
        finally:
            if not stdout.closed:
                stdout.close()

    def _handle_line(self, line):
        raise NotImplementedError
