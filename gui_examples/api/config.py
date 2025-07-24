"""
配置文件
包含服务器配置和常量定义
"""

# C服务器配置
C_SERVER_HOST = '127.0.0.1'
C_SERVER_PORT = 9000

# Web服务器配置
WEB_SERVER_HOST = '0.0.0.0'
WEB_SERVER_PORT = 5000
WEB_SERVER_DEBUG = True

# 文件上传配置
MAX_FILE_SIZE = 100 * 1024 * 1024  # 100MB
ALLOWED_EXTENSIONS = set()  # 空集合表示允许所有文件类型

# 安全配置
SECRET_KEY = 'your-secret-key-here'  # 在生产环境中应该使用环境变量

# 日志配置
LOG_LEVEL = 'DEBUG'
LOG_FORMAT = '%(asctime)s - %(name)s - %(levelname)s - %(message)s'

# 其他配置
DEFAULT_TIMEOUT = 60  # 默认超时时间（秒）- 增加到60秒
UPLOAD_TIMEOUT = 120  # 上传超时时间（秒）- 上传需要更长时间
MAX_RETRIES = 3  # 最大重试次数
