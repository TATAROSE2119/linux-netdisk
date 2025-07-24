"""
C服务器客户端模块
负责与C服务器的通信
"""

import socket
import struct
from .config import C_SERVER_HOST, C_SERVER_PORT, DEFAULT_TIMEOUT


class CServerClient:
    """C服务器客户端类"""
    
    def __init__(self, host=C_SERVER_HOST, port=C_SERVER_PORT, timeout=DEFAULT_TIMEOUT):
        self.host = host
        self.port = port
        self.timeout = timeout
    
    def connect(self):
        """连接到C服务器"""
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(self.timeout)
            sock.connect((self.host, self.port))
            return sock
        except Exception as e:
            print(f"[ERROR] 连接C服务器失败: {e}")
            return None
    
    def send_string(self, sock, string):
        """发送字符串到C服务器"""
        try:
            string_bytes = string.encode('utf-8')
            length = len(string_bytes)
            length_net = struct.pack('!I', length)
            
            print(f"[DEBUG] 发送字符串: '{string}' (长度: {length})")
            
            sock.send(length_net)
            if length > 0:
                sock.send(string_bytes)
        except Exception as e:
            print(f"[ERROR] 发送字符串失败: {e}")
            raise
    
    def recv_response(self, sock):
        """接收C服务器响应"""
        try:
            response = sock.recv(1)
            return response[0] if response else 0
        except Exception as e:
            print(f"[ERROR] 接收响应失败: {e}")
            return 0
    
    def recv_file_list(self, sock):
        """接收文件列表"""
        try:
            # 接收条目数量
            count_data = sock.recv(4)
            if len(count_data) != 4:
                print("[ERROR] 接收条目数量失败")
                return []
            
            print(f"[DEBUG] 接收到条目数量数据: {len(count_data)} 字节")
            count = struct.unpack('!I', count_data)[0]
            print(f"[DEBUG] 条目数量: {count}")
            
            files = []
            for i in range(count):
                print(f"[DEBUG] 处理第 {i + 1} 个条目")
                
                # 接收条目类型
                type_data = sock.recv(4)
                if len(type_data) != 4:
                    print(f"[ERROR] 接收条目 {i + 1} 类型失败")
                    break
                
                entry_type = struct.unpack('!I', type_data)[0]
                print(f"[DEBUG] 条目类型: {entry_type}")
                
                # 接收名称长度
                name_len_data = sock.recv(4)
                if len(name_len_data) != 4:
                    print(f"[ERROR] 接收条目 {i + 1} 名称长度失败")
                    break
                
                name_len = struct.unpack('!I', name_len_data)[0]
                print(f"[DEBUG] 名称长度: {name_len}")
                
                # 接收名称
                name_data = sock.recv(name_len)
                if len(name_data) != name_len:
                    print(f"[ERROR] 接收条目 {i + 1} 名称失败")
                    break
                
                name = name_data.decode('utf-8')
                print(f"[DEBUG] 名称: {name}")
                
                # 接收大小
                size_data = sock.recv(8)
                if len(size_data) != 8:
                    print(f"[ERROR] 接收条目 {i + 1} 大小失败")
                    break
                
                size = struct.unpack('!Q', size_data)[0]
                print(f"[DEBUG] 大小: {size}")
                
                # 接收修改时间
                mtime_data = sock.recv(8)
                if len(mtime_data) != 8:
                    print(f"[ERROR] 接收条目 {i + 1} 修改时间失败")
                    break
                
                mtime = struct.unpack('!Q', mtime_data)[0]
                print(f"[DEBUG] 修改时间: {mtime}")
                
                files.append({
                    'name': name,
                    'type': entry_type,
                    'size': size,
                    'mtime': mtime
                })
            
            print(f"[DEBUG] 成功解析 {len(files)} 个文件")
            return files
            
        except Exception as e:
            print(f"[ERROR] 接收文件列表失败: {e}")
            return []
    
    def recv_file_content(self, sock):
        """接收文件内容"""
        try:
            # 接收文件存在标志
            flag_data = sock.recv(1)
            if len(flag_data) != 1 or flag_data[0] == 0:
                return None, "文件不存在"
            
            # 接收文件大小
            size_high_data = sock.recv(4)
            size_low_data = sock.recv(4)
            size_high = struct.unpack('!I', size_high_data)[0]
            size_low = struct.unpack('!I', size_low_data)[0]
            file_size = (size_high << 32) | size_low
            
            # 接收文件内容
            file_content = b''
            bytes_received = 0
            while bytes_received < file_size:
                chunk = sock.recv(min(4096, file_size - bytes_received))
                if not chunk:
                    break
                file_content += chunk
                bytes_received += len(chunk)
            
            return file_content, None
            
        except Exception as e:
            print(f"[ERROR] 接收文件内容失败: {e}")
            return None, str(e)
    
    def test_connection(self):
        """测试连接"""
        sock = self.connect()
        if sock:
            sock.close()
            return True
        return False


# 创建全局实例
c_client = CServerClient()
