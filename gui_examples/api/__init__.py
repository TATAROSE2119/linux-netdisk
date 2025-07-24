"""
API模块初始化文件
"""

from .config import *
from .c_server_client import c_client
from .auth_routes import auth_bp
from .file_routes import file_bp

__all__ = [
    'c_client',
    'auth_bp', 
    'file_bp'
]
