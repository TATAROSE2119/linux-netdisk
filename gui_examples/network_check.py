#!/usr/bin/env python3
"""
网络连接诊断脚本
检查端口转发和外网访问状态
"""

import socket
import subprocess
import requests
import time
from api.config import DOMAIN_NAME, WEB_SERVER_PORT

def get_local_ip():
    """获取本机内网IP"""
    try:
        result = subprocess.run(['hostname', '-I'], capture_output=True, text=True)
        return result.stdout.strip().split()[0]
    except:
        return "未知"

def get_gateway_ip():
    """获取网关IP"""
    try:
        result = subprocess.run(['ip', 'route', 'show', 'default'], capture_output=True, text=True)
        for line in result.stdout.split('\n'):
            if 'default via' in line:
                return line.split()[2]
    except:
        return "未知"

def get_public_ip():
    """获取公网IP"""
    try:
        response = requests.get('https://api.ipify.org', timeout=10)
        return response.text.strip()
    except:
        try:
            response = requests.get('https://icanhazip.com', timeout=10)
            return response.text.strip()
        except:
            return "无法获取"

def check_local_port(port):
    """检查本地端口是否开放"""
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(3)
        result = sock.connect_ex(('127.0.0.1', port))
        sock.close()
        return result == 0
    except:
        return False

def check_domain_resolution(domain):
    """检查域名解析"""
    try:
        import socket
        ip = socket.gethostbyname(domain)
        return ip
    except:
        return "解析失败"

def test_external_access(domain, port):
    """测试外网访问"""
    try:
        url = f"http://{domain}:{port}/api/health"
        response = requests.get(url, timeout=10)
        return response.status_code == 200
    except:
        return False

def main():
    """主函数"""
    print("🌐 网络连接诊断工具")
    print("=" * 50)
    
    # 基本网络信息
    local_ip = get_local_ip()
    gateway_ip = get_gateway_ip()
    public_ip = get_public_ip()
    
    print(f"📍 本机内网IP: {local_ip}")
    print(f"🚪 网关IP: {gateway_ip}")
    print(f"🌍 公网IP: {public_ip}")
    print()
    
    # 域名解析检查
    domain_ip = check_domain_resolution(DOMAIN_NAME)
    print(f"🔍 域名解析: {DOMAIN_NAME} -> {domain_ip}")
    
    if domain_ip == public_ip:
        print("✅ 域名解析正确")
    else:
        print("❌ 域名解析不匹配公网IP")
    print()
    
    # 本地端口检查
    local_port_open = check_local_port(WEB_SERVER_PORT)
    print(f"🔌 本地端口 {WEB_SERVER_PORT}: {'✅ 开放' if local_port_open else '❌ 关闭'}")
    
    if not local_port_open:
        print("   请确保Web服务器正在运行")
        return
    
    # 外网访问测试
    print(f"🌐 测试外网访问: {DOMAIN_NAME}:{WEB_SERVER_PORT}")
    external_access = test_external_access(DOMAIN_NAME, WEB_SERVER_PORT)
    
    if external_access:
        print("✅ 外网访问正常")
    else:
        print("❌ 外网访问失败")
        print()
        print("🔧 可能的解决方案:")
        print("1. 配置路由器端口转发:")
        print(f"   - 外部端口: {WEB_SERVER_PORT}")
        print(f"   - 内部IP: {local_ip}")
        print(f"   - 内部端口: {WEB_SERVER_PORT}")
        print(f"   - 路由器管理地址: http://{gateway_ip}")
        print()
        print("2. 检查防火墙设置:")
        print(f"   sudo ufw allow {WEB_SERVER_PORT}")
        print()
        print("3. 检查ISP是否封锁端口")
    
    print()
    print("=" * 50)
    print("🔗 访问地址:")
    print(f"   本地: http://localhost:{WEB_SERVER_PORT}")
    print(f"   内网: http://{local_ip}:{WEB_SERVER_PORT}")
    print(f"   外网: http://{DOMAIN_NAME}:{WEB_SERVER_PORT}")

if __name__ == '__main__':
    main()
