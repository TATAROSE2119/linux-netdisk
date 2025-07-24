"""
认证相关路由
包含登录、注册等功能
"""

from flask import Blueprint, request, jsonify
import hashlib
from .c_server_client import c_client

# 创建认证蓝图
auth_bp = Blueprint('auth', __name__, url_prefix='/api')


@auth_bp.route('/login', methods=['POST'])
def login():
    """用户登录API"""
    data = request.json
    username = data.get('username', '')
    password = data.get('password', '')
    
    if not username or not password:
        return jsonify({'success': False, 'message': '用户名和密码不能为空'})
    
    # 连接C服务器
    sock = c_client.connect()
    if not sock:
        return jsonify({'success': False, 'message': '无法连接到服务器'})
    
    try:
        # 发送登录命令
        sock.send(b'L')
        
        # 发送用户名和密码
        c_client.send_string(sock, username)
        c_client.send_string(sock, password)
        
        # 接收响应
        result = c_client.recv_response(sock)
        
        if result == 1:
            return jsonify({'success': True, 'message': '登录成功'})
        else:
            return jsonify({'success': False, 'message': '用户名或密码错误'})
            
    except Exception as e:
        return jsonify({'success': False, 'message': f'登录失败: {str(e)}'})
    finally:
        sock.close()


@auth_bp.route('/register', methods=['POST'])
def register():
    """用户注册API"""
    data = request.json
    username = data.get('username', '')
    password = data.get('password', '')
    
    if not username or not password:
        return jsonify({'success': False, 'message': '用户名和密码不能为空'})
    
    # 验证用户名格式
    if len(username) < 3:
        return jsonify({'success': False, 'message': '用户名至少需要3个字符'})
    
    if len(username) > 20:
        return jsonify({'success': False, 'message': '用户名不能超过20个字符'})
    
    # 验证密码格式
    if len(password) < 6:
        return jsonify({'success': False, 'message': '密码至少需要6个字符'})
    
    if len(password) > 50:
        return jsonify({'success': False, 'message': '密码不能超过50个字符'})
    
    # 连接C服务器
    sock = c_client.connect()
    if not sock:
        return jsonify({'success': False, 'message': '无法连接到服务器'})
    
    try:
        # 发送注册命令
        sock.send(b'R')
        
        # 发送用户名和密码
        c_client.send_string(sock, username)
        c_client.send_string(sock, password)
        
        # 接收响应
        result = c_client.recv_response(sock)
        
        if result == 1:
            return jsonify({'success': True, 'message': '注册成功'})
        elif result == 2:
            return jsonify({'success': False, 'message': '用户名已存在'})
        else:
            return jsonify({'success': False, 'message': '注册失败'})
            
    except Exception as e:
        return jsonify({'success': False, 'message': f'注册失败: {str(e)}'})
    finally:
        sock.close()


def validate_username(username):
    """验证用户名格式"""
    if not username or len(username.strip()) == 0:
        return False, '用户名不能为空'
    
    username = username.strip()
    
    if len(username) < 3:
        return False, '用户名至少需要3个字符'
    
    if len(username) > 20:
        return False, '用户名不能超过20个字符'
    
    # 检查是否包含特殊字符
    import re
    if not re.match(r'^[a-zA-Z0-9_-]+$', username):
        return False, '用户名只能包含字母、数字、下划线和连字符'
    
    return True, '用户名格式正确'


def validate_password(password):
    """验证密码格式"""
    if not password or len(password) == 0:
        return False, '密码不能为空'
    
    if len(password) < 6:
        return False, '密码至少需要6个字符'
    
    if len(password) > 50:
        return False, '密码不能超过50个字符'
    
    return True, '密码格式正确'


def hash_password(password):
    """密码哈希"""
    return hashlib.sha256(password.encode()).hexdigest()


def verify_password(password, hashed):
    """验证密码"""
    return hash_password(password) == hashed
