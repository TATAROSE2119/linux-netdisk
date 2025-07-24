"""
主应用文件
重构后的Web桥接服务器
"""

from flask import Flask, send_from_directory
from flask_cors import CORS
import os
import sys

# 添加当前目录到Python路径
sys.path.append(os.path.dirname(os.path.abspath(__file__)))

from api.config import WEB_SERVER_HOST, WEB_SERVER_PORT, WEB_SERVER_DEBUG
from api.auth_routes import auth_bp
from api.file_routes import file_bp
from api.c_server_client import c_client


def create_app():
    """创建Flask应用"""
    app = Flask(__name__)
    CORS(app)  # 允许跨域请求
    
    # 注册蓝图
    app.register_blueprint(auth_bp)
    app.register_blueprint(file_bp)
    
    return app


# 创建应用实例
app = create_app()


# 静态文件路由
@app.route('/')
def index():
    """主页 - 返回登录页面"""
    return send_from_directory('.', 'login.html')


@app.route('/login.html')
def login_page():
    """登录页面"""
    return send_from_directory('.', 'login.html')


@app.route('/dashboard.html')
def dashboard_page():
    """主功能界面"""
    return send_from_directory('.', 'dashboard.html')


@app.route('/web_client.html')
def old_client():
    """旧版客户端（兼容性）"""
    return send_from_directory('.', 'web_client.html')


# 静态资源路由
@app.route('/js/<path:filename>')
def serve_js(filename):
    """提供JavaScript文件"""
    return send_from_directory('js', filename)


@app.route('/css/<path:filename>')
def serve_css(filename):
    """提供CSS文件"""
    return send_from_directory('css', filename)


@app.route('/api/health')
def health_check():
    """健康检查API"""
    c_server_status = c_client.test_connection()
    return {
        'status': 'ok',
        'c_server_connected': c_server_status,
        'message': 'Web桥接服务器运行正常'
    }


def print_startup_info():
    """打印启动信息"""
    print("=" * 50)
    print("🌐 Web桥接服务器启动中...")
    print(f"📡 连接到C服务器: {c_client.host}:{c_client.port}")
    print(f"🌍 Web界面地址: http://localhost:{WEB_SERVER_PORT}")
    print("=" * 50)


if __name__ == '__main__':
    print_startup_info()
    
    # 测试C服务器连接
    if not c_client.test_connection():
        print("⚠️  警告: 无法连接到C服务器，请确保C服务器正在运行")
    
    # 启动Web服务器
    app.run(
        host=WEB_SERVER_HOST,
        port=WEB_SERVER_PORT,
        debug=WEB_SERVER_DEBUG
    )
