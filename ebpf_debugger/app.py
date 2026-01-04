"""
eBPF Debugger - Main Flask Application

Web dashboard for monitoring network traffic, syscalls, and performance
"""

import os
import sys
import threading
import time
import signal
import psutil

from flask import Flask, render_template, jsonify
from flask_socketio import SocketIO

# 添加父目录到路径
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import config
from api import api_bp, set_collectors
from collectors import NetworkCollector, SyscallCollector, PerfCollector

# Flask应用
app = Flask(__name__)
app.config['SECRET_KEY'] = config.SECRET_KEY
socketio = SocketIO(app, cors_allowed_origins="*", async_mode='threading')

# 数据收集器
network_collector = NetworkCollector()
syscall_collector = SyscallCollector()
perf_collector = PerfCollector()

# 设置API收集器引用
set_collectors(network_collector, syscall_collector, perf_collector)

# 监控器实例(延迟初始化)
monitors = {
    'network': None,
    'syscall': None,
    'perf': None,
    'uprobe': None
}

# 运行状态
running = False
monitor_thread = None


def find_target_pids():
    """查找目标进程PID"""
    pids = []
    for proc in psutil.process_iter(['pid', 'name', 'cmdline']):
        try:
            name = proc.info['name']
            if name in ['server', 'client']:
                pids.append(proc.info['pid'])
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            pass
    return pids


def init_monitors():
    """初始化eBPF监控器"""
    global monitors

    try:
        # 导入eBPF模块(需要root权限)
        from bpf_programs.network_monitor import NetworkMonitor
        from bpf_programs.syscall_tracer import SyscallTracer
        from bpf_programs.perf_analyzer import PerfAnalyzer
        from bpf_programs.uprobe_tracer import UprobeTracer

        # 初始化网络监控
        if config.ENABLE_NETWORK_MONITOR:
            monitors['network'] = NetworkMonitor(network_collector)

        # 初始化系统调用追踪
        if config.ENABLE_SYSCALL_TRACER:
            monitors['syscall'] = SyscallTracer(syscall_collector)

        # 初始化性能分析
        if config.ENABLE_PERF_ANALYZER:
            target_pids = find_target_pids()
            monitors['perf'] = PerfAnalyzer(perf_collector, target_pids=target_pids if target_pids else None)

        # 初始化uprobe追踪
        if config.ENABLE_UPROBE_TRACER:
            monitors['uprobe'] = UprobeTracer(
                perf_collector,
                server_path=config.SERVER_PATH,
                client_path=config.CLIENT_PATH
            )

        return True

    except ImportError as e:
        print(f"[Warning] eBPF modules not available: {e}")
        print("[Info] Running in demo mode with simulated data")
        return False

    except Exception as e:
        print(f"[Error] Failed to initialize monitors: {e}")
        return False


def start_monitors():
    """启动所有监控器"""
    for name, monitor in monitors.items():
        if monitor:
            try:
                monitor.start()
            except Exception as e:
                print(f"[Error] Failed to start {name} monitor: {e}")


def stop_monitors():
    """停止所有监控器"""
    for name, monitor in monitors.items():
        if monitor:
            try:
                monitor.stop()
            except Exception as e:
                print(f"[Error] Failed to stop {name} monitor: {e}")


def monitor_loop():
    """监控主循环"""
    global running

    while running:
        # 轮询所有监控器
        for monitor in monitors.values():
            if monitor:
                try:
                    monitor.poll(timeout=10)
                except Exception as e:
                    print(f"[Error] Monitor poll error: {e}")

        # 更新历史数据
        network_collector.update_history()
        syscall_collector.update_history()
        perf_collector.update_history()

        # 通过WebSocket推送数据
        socketio.emit('update', {
            'network': network_collector.get_stats(),
            'syscall': syscall_collector.get_stats(),
            'perf': perf_collector.get_stats()
        })

        time.sleep(config.SAMPLE_INTERVAL)


# 路由
@app.route('/')
def index():
    """主页"""
    return render_template('dashboard.html')


@app.route('/status')
def status():
    """状态页"""
    return jsonify({
        'running': running,
        'monitors': {name: m is not None for name, m in monitors.items()},
        'config': {
            'host': config.HOST,
            'port': config.PORT,
            'target_ports': config.TARGET_PORTS
        }
    })


# 注册API蓝图
app.register_blueprint(api_bp)


# WebSocket事件
@socketio.on('connect')
def handle_connect():
    print('[WebSocket] Client connected')


@socketio.on('disconnect')
def handle_disconnect():
    print('[WebSocket] Client disconnected')


@socketio.on('start_monitoring')
def handle_start():
    global running, monitor_thread

    if not running:
        running = True
        start_monitors()
        monitor_thread = threading.Thread(target=monitor_loop, daemon=True)
        monitor_thread.start()
        return {'status': 'started'}
    return {'status': 'already_running'}


@socketio.on('stop_monitoring')
def handle_stop():
    global running

    if running:
        running = False
        stop_monitors()
        return {'status': 'stopped'}
    return {'status': 'not_running'}


def signal_handler(sig, frame):
    """处理退出信号"""
    global running
    print("\n[Info] Shutting down...")
    running = False
    stop_monitors()
    sys.exit(0)


def main():
    """主函数"""
    global running, monitor_thread

    # 注册信号处理
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    print("=" * 60)
    print("  eBPF Debugger for NetDisk")
    print("=" * 60)
    print(f"  Server Path: {config.SERVER_PATH}")
    print(f"  Client Path: {config.CLIENT_PATH}")
    print(f"  Target Ports: {config.TARGET_PORTS}")
    print("=" * 60)

    # 初始化监控器
    ebpf_available = init_monitors()

    if ebpf_available:
        # 自动启动监控
        running = True
        start_monitors()
        monitor_thread = threading.Thread(target=monitor_loop, daemon=True)
        monitor_thread.start()
        print("[Info] eBPF monitoring started")
    else:
        print("[Info] eBPF not available, dashboard will show no data")

    print(f"\n[Info] Dashboard available at http://localhost:{config.PORT}")
    print("[Info] Press Ctrl+C to stop\n")

    # 启动Flask
    socketio.run(app, host=config.HOST, port=config.PORT, debug=False)


if __name__ == '__main__':
    main()
