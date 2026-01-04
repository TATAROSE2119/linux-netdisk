"""eBPF Debugger Configuration"""

import os

# Flask配置
DEBUG = True
HOST = '0.0.0.0'
PORT = 8090
SECRET_KEY = os.urandom(24)

# 监控目标
TARGET_PORTS = [9000, 8080]  # 网盘服务器端口
SERVER_PATH = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'server', 'server'))
CLIENT_PATH = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'client', 'client'))

# 数据收集配置
SAMPLE_INTERVAL = 1  # 采样间隔(秒)
MAX_EVENTS = 1000    # 最大事件缓存数
HISTORY_SIZE = 60    # 历史数据点数(用于图表)

# eBPF配置
ENABLE_NETWORK_MONITOR = True
ENABLE_SYSCALL_TRACER = True
ENABLE_PERF_ANALYZER = True
ENABLE_UPROBE_TRACER = True
