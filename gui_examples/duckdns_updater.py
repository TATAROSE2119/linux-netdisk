import requests
import time
import threading
from api.config import DUCKDNS_DOMAIN, DUCKDNS_TOKEN, AUTO_UPDATE_IP

class DuckDNSUpdater:
    def __init__(self):
        self.domain = DUCKDNS_DOMAIN
        self.token = DUCKDNS_TOKEN
        self.last_ip = None
        
    def get_public_ip(self):
        """获取公网IP地址"""
        try:
            response = requests.get('https://ifconfig.me', timeout=10)
            return response.text.strip()
        except:
            try:
                response = requests.get('https://api.ipify.org', timeout=10)
                return response.text.strip()
            except:
                return None
    
    def update_duckdns(self, ip=None):
        """更新DuckDNS记录"""
        if ip is None:
            ip = self.get_public_ip()
            
        if ip is None:
            print("❌ 无法获取公网IP地址")
            return False
            
        url = f"https://www.duckdns.org/update?domains={self.domain}&token={self.token}&ip={ip}"
        
        try:
            response = requests.get(url, timeout=10)
            if response.text.strip() == "OK":
                print(f"✅ DuckDNS更新成功: {self.domain}.duckdns.org -> {ip}")
                self.last_ip = ip
                return True
            else:
                print(f"❌ DuckDNS更新失败: {response.text}")
                return False
        except Exception as e:
            print(f"❌ DuckDNS更新异常: {e}")
            return False
    
    def start_auto_update(self, interval=300):
        """启动自动更新（每5分钟检查一次）"""
        def update_loop():
            while True:
                current_ip = self.get_public_ip()
                if current_ip and current_ip != self.last_ip:
                    self.update_duckdns(current_ip)
                time.sleep(interval)
        
        if AUTO_UPDATE_IP:
            thread = threading.Thread(target=update_loop, daemon=True)
            thread.start()
            print(f"🔄 DuckDNS自动更新已启动 (间隔: {interval}秒)")

# 全局实例
duckdns_updater = DuckDNSUpdater()