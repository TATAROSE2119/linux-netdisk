"""
主应用文件
重构后的Web桥接服务器
"""

from flask import Flask, send_from_directory
from flask_cors import CORS
import os
import sys
from api.config import DOMAIN_NAME, WEB_SERVER_HOST, WEB_SERVER_PORT, WEB_SERVER_DEBUG

# 添加当前目录到Python路径
sys.path.append(os.path.dirname(os.path.abspath(__file__)))

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
    print("=" * 60)
    print("🌐 网盘Web服务器启动中...")
    print(f"📡 C服务器地址: {c_client.host}:{c_client.port}")
    print(f"🌍 访问地址: http://{DOMAIN_NAME}")
    print(f"🔗 本地访问: http://localhost:{WEB_SERVER_PORT}")
    print("=" * 60)


if __name__ == '__main__':
    # 支持环境变量覆盖端口
    port = int(os.environ.get('WEB_SERVER_PORT', WEB_SERVER_PORT))

    print_startup_info()

    # 测试C服务器连接
    if not c_client.test_connection():
        print("⚠️  警告: 无法连接到C服务器，请确保C服务器正在运行")

    # 启动Web服务器
    try:
        app.run(
            host=WEB_SERVER_HOST,
            port=port,
            debug=WEB_SERVER_DEBUG
        )
    except PermissionError:
        print(f"❌ 端口{port}需要管理员权限，尝试使用端口8080...")
        app.run(
            host=WEB_SERVER_HOST,
            port=8080,
            debug=WEB_SERVER_DEBUG
        )
