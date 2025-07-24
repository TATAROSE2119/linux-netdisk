"""
文件操作相关路由
包含文件列表、上传、下载、删除等功能
"""

from flask import Blueprint, request, jsonify, Response
import os
import zipfile
import io
import socket
import struct
from .c_server_client import c_client
from .config import UPLOAD_TIMEOUT, C_SERVER_HOST, C_SERVER_PORT

# 创建文件操作蓝图
file_bp = Blueprint('file', __name__, url_prefix='/api')


@file_bp.route('/files', methods=['GET'])
def get_files():
    """获取文件列表API"""
    username = request.args.get('username', '')
    path = request.args.get('path', '/')
    
    if not username:
        return jsonify({'success': False, 'message': '用户名不能为空'})
    
    print(f"[DEBUG] 获取文件列表: 用户={username}, 路径={path}")
    
    # 连接C服务器
    sock = c_client.connect()
    if not sock:
        return jsonify({'success': False, 'message': '无法连接到服务器'})
    
    try:
        # 发送获取文件列表命令
        sock.send(b'S')
        
        # 发送用户名
        c_client.send_string(sock, username)
        
        # 发送路径（去掉开头的斜杠，因为C服务器期望相对路径）
        relative_path = path[1:] if path.startswith('/') and len(path) > 1 else ''
        c_client.send_string(sock, relative_path)
        
        # 接收响应
        result = c_client.recv_response(sock)
        print(f"[DEBUG] 服务器响应: {result}")
        
        if result == 1:
            # 接收文件列表
            files = c_client.recv_file_list(sock)
            return jsonify({'success': True, 'files': files})
        else:
            return jsonify({'success': False, 'message': '获取文件列表失败'})
            
    except Exception as e:
        print(f"[ERROR] 获取文件列表失败: {e}")
        return jsonify({'success': False, 'message': f'获取文件列表失败: {str(e)}'})
    finally:
        sock.close()


def create_upload_connection():
    """创建专门用于上传的连接，使用更长的超时时间"""
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(UPLOAD_TIMEOUT)  # 使用更长的超时时间
        sock.connect((C_SERVER_HOST, C_SERVER_PORT))
        return sock
    except Exception as e:
        print(f"[ERROR] 创建上传连接失败: {e}")
        return None

def send_string_upload(sock, string):
    """发送字符串到C服务器（上传专用）"""
    try:
        string_bytes = string.encode('utf-8')
        length = len(string_bytes)
        length_net = struct.pack('!I', length)

        print(f"[DEBUG] 上传发送字符串: '{string}' (长度: {length})")

        sock.send(length_net)
        if length > 0:
            sock.send(string_bytes)
    except Exception as e:
        print(f"[ERROR] 上传发送字符串失败: {e}")
        raise

def recv_upload_response(sock):
    """接收上传响应"""
    try:
        print("[DEBUG] 等待上传响应...")
        response = sock.recv(1)
        if response:
            result = response[0]
            print(f"[DEBUG] 上传响应: {result}")
            return result
        else:
            print("[ERROR] 未收到上传响应")
            return 0
    except socket.timeout:
        print("[ERROR] 上传响应超时")
        return 0
    except Exception as e:
        print(f"[ERROR] 接收上传响应失败: {e}")
        return 0

@file_bp.route('/upload', methods=['POST'])
def upload_file():
    """文件上传API"""
    if 'file' not in request.files:
        return jsonify({'success': False, 'message': '没有选择文件'})

    file = request.files['file']
    username = request.form.get('username', '')
    path = request.form.get('path', '/')

    if not username:
        return jsonify({'success': False, 'message': '用户名不能为空'})

    if file.filename == '':
        return jsonify({'success': False, 'message': '没有选择文件'})

    print(f"[DEBUG] 开始上传文件: {file.filename}, 用户: {username}, 路径: {path}")

    # 创建专门的上传连接
    sock = create_upload_connection()
    if not sock:
        return jsonify({'success': False, 'message': '无法连接到服务器'})

    try:
        # 发送上传命令
        sock.send(b'U')
        print("[DEBUG] 已发送上传命令")

        # 发送用户名
        send_string_upload(sock, username)

        # 发送路径（去掉开头的斜杠）
        relative_path = path[1:] if path.startswith('/') and len(path) > 1 else ''
        send_string_upload(sock, relative_path)

        # 发送文件名
        send_string_upload(sock, file.filename)

        # 读取文件内容
        file_content = file.read()
        file_size = len(file_content)
        print(f"[DEBUG] 文件大小: {file_size} 字节")

        # 发送文件大小（分为高32位和低32位）
        size_high = (file_size >> 32) & 0xFFFFFFFF
        size_low = file_size & 0xFFFFFFFF

        sock.send(struct.pack('!I', size_high))
        sock.send(struct.pack('!I', size_low))
        print(f"[DEBUG] 已发送文件大小: high={size_high}, low={size_low}")

        # 发送文件内容
        sock.send(file_content)
        print("[DEBUG] 已发送文件内容")

        # 接收响应
        result = recv_upload_response(sock)

        if result == 1:
            print("[DEBUG] 上传成功")
            return jsonify({'success': True, 'message': '文件上传成功'})
        elif result == 0:
            # 没有收到响应，但文件可能已经上传成功
            print("[DEBUG] 未收到响应，但文件可能已上传")
            return jsonify({'success': True, 'message': '文件上传完成（服务器无响应，请刷新检查）'})
        else:
            print(f"[DEBUG] 上传失败，响应码: {result}")
            return jsonify({'success': False, 'message': '文件上传失败'})

    except Exception as e:
        print(f"[ERROR] 上传异常: {e}")
        return jsonify({'success': False, 'message': f'文件上传失败: {str(e)}'})
    finally:
        sock.close()
        print("[DEBUG] 上传连接已关闭")


@file_bp.route('/download', methods=['POST'])
def download_file():
    """文件下载API"""
    data = request.json
    username = data.get('username', '')
    file_path = data.get('filePath', '')
    
    if not username or not file_path:
        return jsonify({'success': False, 'message': '参数不完整'})
    
    # 连接C服务器
    sock = c_client.connect()
    if not sock:
        return jsonify({'success': False, 'message': '无法连接到服务器'})
    
    try:
        # 发送下载命令
        sock.send(b'D')
        
        # 发送用户名
        c_client.send_string(sock, username)
        
        # 发送文件名（从路径中提取文件名）
        filename = file_path.split('/')[-1]
        c_client.send_string(sock, filename)
        
        # 接收文件内容
        file_content, error = c_client.recv_file_content(sock)
        
        if file_content is None:
            return jsonify({'success': False, 'message': error or '文件不存在'})
        
        # 返回文件内容
        return Response(
            file_content,
            mimetype='application/octet-stream',
            headers={
                'Content-Disposition': f'attachment; filename="{filename}"',
                'Content-Length': str(len(file_content))
            }
        )
        
    except Exception as e:
        return jsonify({'success': False, 'message': f'下载失败: {str(e)}'})
    finally:
        sock.close()


@file_bp.route('/batch-download', methods=['POST'])
def batch_download():
    """批量下载文件API"""
    data = request.json
    username = data.get('username', '')
    file_paths = data.get('filePaths', [])
    zip_name = data.get('zipName', 'files.zip')
    
    if not username or not file_paths:
        return jsonify({'success': False, 'message': '参数不完整'})
    
    print(f"[DEBUG] 批量下载请求: 用户={username}, 文件数量={len(file_paths)}")
    
    try:
        # 创建内存中的ZIP文件
        zip_buffer = io.BytesIO()
        
        with zipfile.ZipFile(zip_buffer, 'w', zipfile.ZIP_DEFLATED) as zip_file:
            for file_path in file_paths:
                try:
                    # 连接C服务器下载单个文件
                    sock = c_client.connect()
                    if not sock:
                        continue
                    
                    # 发送下载命令
                    sock.send(b'D')
                    
                    # 发送用户名
                    c_client.send_string(sock, username)
                    
                    # 发送文件名（从路径中提取文件名）
                    filename = file_path.split('/')[-1]
                    c_client.send_string(sock, filename)
                    
                    # 接收文件内容
                    file_content, error = c_client.recv_file_content(sock)
                    
                    if file_content is not None:
                        # 添加到ZIP文件
                        zip_file.writestr(filename, file_content)
                        print(f"[DEBUG] 已添加到ZIP: {filename} ({len(file_content)} 字节)")
                    else:
                        print(f"[DEBUG] 文件下载失败: {filename} - {error}")
                    
                    sock.close()
                    
                except Exception as e:
                    print(f"[DEBUG] 下载文件失败 {file_path}: {str(e)}")
                    continue
        
        # 获取ZIP文件内容
        zip_buffer.seek(0)
        zip_content = zip_buffer.getvalue()
        zip_buffer.close()
        
        if len(zip_content) == 0:
            return jsonify({'success': False, 'message': '没有成功下载任何文件'})
        
        print(f"[DEBUG] ZIP文件创建完成，大小: {len(zip_content)} 字节")
        
        # 返回ZIP文件
        return Response(
            zip_content,
            mimetype='application/zip',
            headers={
                'Content-Disposition': f'attachment; filename="{zip_name}"',
                'Content-Length': str(len(zip_content))
            }
        )
        
    except Exception as e:
        print(f"[DEBUG] 批量下载失败: {str(e)}")
        return jsonify({'success': False, 'message': f'批量下载失败: {str(e)}'})


@file_bp.route('/delete', methods=['POST'])
def delete_file():
    """删除文件或目录API"""
    data = request.json
    username = data.get('username', '')
    file_path = data.get('filePath', '')
    
    if not username or not file_path:
        return jsonify({'success': False, 'message': '参数不完整'})
    
    # 连接C服务器
    sock = c_client.connect()
    if not sock:
        return jsonify({'success': False, 'message': '无法连接到服务器'})
    
    try:
        # 发送删除命令
        sock.send(b'X')
        
        # 发送用户名
        c_client.send_string(sock, username)
        
        # 发送文件路径
        c_client.send_string(sock, file_path)
        
        # 接收响应
        result = c_client.recv_response(sock)
        
        if result == 1:
            return jsonify({'success': True, 'message': '删除成功'})
        else:
            return jsonify({'success': False, 'message': '删除失败'})
            
    except Exception as e:
        return jsonify({'success': False, 'message': f'删除失败: {str(e)}'})
    finally:
        sock.close()


@file_bp.route('/mkdir', methods=['POST'])
def create_directory():
    """创建目录API"""
    data = request.json
    username = data.get('username', '')
    path = data.get('path', '')
    
    if not username or not path:
        return jsonify({'success': False, 'message': '参数不完整'})
    
    # 连接C服务器
    sock = c_client.connect()
    if not sock:
        return jsonify({'success': False, 'message': '无法连接到服务器'})
    
    try:
        # 发送创建目录命令
        sock.send(b'M')
        
        # 发送用户名
        c_client.send_string(sock, username)
        
        # 发送路径
        c_client.send_string(sock, path)
        
        # 接收响应
        result = c_client.recv_response(sock)
        
        if result == 1:
            return jsonify({'success': True, 'message': '目录创建成功'})
        else:
            return jsonify({'success': False, 'message': '目录创建失败'})
            
    except Exception as e:
        return jsonify({'success': False, 'message': f'目录创建失败: {str(e)}'})
    finally:
        sock.close()


@file_bp.route('/rename', methods=['POST'])
def rename_item():
    """重命名文件或目录API"""
    data = request.json
    username = data.get('username', '')
    old_path = data.get('oldPath', '')
    new_path = data.get('newPath', '')
    
    if not username or not old_path or not new_path:
        return jsonify({'success': False, 'message': '参数不完整'})
    
    # 连接C服务器
    sock = c_client.connect()
    if not sock:
        return jsonify({'success': False, 'message': '无法连接到服务器'})
    
    try:
        print(f"[DEBUG] 重命名请求: 用户={username}, 旧路径={old_path}, 新路径={new_path}")
        
        # 发送重命名命令
        sock.send(b'N')
        
        # 发送用户名
        c_client.send_string(sock, username)
        
        # 发送旧路径
        c_client.send_string(sock, old_path)
        
        # 发送新路径
        c_client.send_string(sock, new_path)
        
        # 接收响应
        result = c_client.recv_response(sock)
        print(f"[DEBUG] 重命名响应: {result}")
        
        if result == 1:
            return jsonify({'success': True, 'message': '重命名成功'})
        else:
            return jsonify({'success': False, 'message': '重命名失败'})
            
    except Exception as e:
        return jsonify({'success': False, 'message': f'重命名失败: {str(e)}'})
    finally:
        sock.close()
