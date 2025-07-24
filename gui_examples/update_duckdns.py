#!/usr/bin/env python3
"""
DuckDNS IP地址自动更新脚本
"""

import requests
import sys
from api.config import DUCKDNS_DOMAIN, DUCKDNS_TOKEN, AUTO_UPDATE_IP

def get_public_ip():
    """获取公网IP地址"""
    try:
        # 使用多个服务获取公网IP
        services = [
            'https://api.ipify.org',
            'https://icanhazip.com',
            'https://ipecho.net/plain'
        ]
        
        for service in services:
            try:
                response = requests.get(service, timeout=10)
                if response.status_code == 200:
                    ip = response.text.strip()
                    print(f"✅ 获取到公网IP: {ip}")
                    return ip
            except:
                continue
                
        print("❌ 无法获取公网IP地址")
        return None
        
    except Exception as e:
        print(f"❌ 获取公网IP失败: {e}")
        return None

def update_duckdns(domain, token, ip=None):
    """更新DuckDNS域名解析"""
    try:
        if ip is None:
            ip = get_public_ip()
            if ip is None:
                return False
        
        # DuckDNS更新URL
        url = f"https://www.duckdns.org/update?domains={domain}&token={token}&ip={ip}"
        
        print(f"🔄 更新DuckDNS域名: {domain}.duckdns.org -> {ip}")
        
        response = requests.get(url, timeout=30)
        
        if response.status_code == 200 and response.text.strip() == 'OK':
            print(f"✅ DuckDNS更新成功: {domain}.duckdns.org -> {ip}")
            return True
        else:
            print(f"❌ DuckDNS更新失败: {response.text}")
            return False
            
    except Exception as e:
        print(f"❌ DuckDNS更新异常: {e}")
        return False

def main():
    """主函数"""
    print("🌐 DuckDNS IP地址更新工具")
    print("=" * 40)
    
    if not AUTO_UPDATE_IP:
        print("ℹ️  自动更新已禁用")
        return
    
    if not DUCKDNS_DOMAIN or not DUCKDNS_TOKEN:
        print("❌ DuckDNS配置不完整")
        print("请在 api/config.py 中配置 DUCKDNS_DOMAIN 和 DUCKDNS_TOKEN")
        return
    
    # 更新DuckDNS
    success = update_duckdns(DUCKDNS_DOMAIN, DUCKDNS_TOKEN)
    
    if success:
        print("=" * 40)
        print(f"🌍 域名访问地址: http://{DUCKDNS_DOMAIN}.duckdns.org")
        print("=" * 40)
    else:
        print("❌ 更新失败，请检查网络连接和配置")
        sys.exit(1)

if __name__ == '__main__':
    main()
