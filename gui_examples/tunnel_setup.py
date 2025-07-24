#!/usr/bin/env python3
"""
内网穿透设置脚本
提供多种外网访问方案
"""

import os
import subprocess
import sys
from api.config import WEB_SERVER_PORT

def print_banner():
    """打印横幅"""
    print("🌐 内网穿透设置工具")
    print("=" * 50)
    print("由于无法配置路由器端口转发，提供以下替代方案：")
    print()

def check_ngrok():
    """检查ngrok是否可用"""
    try:
        result = subprocess.run(['which', 'ngrok'], capture_output=True)
        return result.returncode == 0
    except:
        return False

def install_ngrok_snap():
    """使用snap安装ngrok"""
    print("📦 尝试使用snap安装ngrok...")
    try:
        subprocess.run(['sudo', 'snap', 'install', 'ngrok'], check=True)
        return True
    except:
        return False

def setup_ngrok():
    """设置ngrok"""
    print("🚀 方案1: 使用ngrok内网穿透")
    print("-" * 30)
    
    if not check_ngrok():
        print("❌ ngrok未安装")
        if install_ngrok_snap():
            print("✅ ngrok安装成功")
        else:
            print("❌ ngrok安装失败")
            print("请手动安装: https://ngrok.com/download")
            return False
    
    print("✅ ngrok已安装")
    print()
    print("📋 使用步骤:")
    print("1. 注册ngrok账号: https://dashboard.ngrok.com/signup")
    print("2. 获取认证令牌")
    print("3. 运行以下命令:")
    print(f"   ngrok config add-authtoken YOUR_TOKEN")
    print(f"   ngrok http {WEB_SERVER_PORT}")
    print()
    print("4. 复制显示的公网地址，如: https://abc123.ngrok.io")
    print()
    return True

def setup_ssh_tunnel():
    """设置SSH隧道"""
    print("🔐 方案2: 使用SSH隧道")
    print("-" * 30)
    print("如果你有一台公网服务器，可以使用SSH隧道:")
    print()
    print("📋 使用步骤:")
    print("1. 在公网服务器上运行:")
    print(f"   ssh -R 0.0.0.0:{WEB_SERVER_PORT}:localhost:{WEB_SERVER_PORT} user@your-server.com")
    print()
    print("2. 访问地址:")
    print(f"   http://your-server.com:{WEB_SERVER_PORT}")
    print()

def setup_frp():
    """设置frp"""
    print("⚡ 方案3: 使用frp内网穿透")
    print("-" * 30)
    print("frp是一个高性能的反向代理应用:")
    print()
    print("📋 使用步骤:")
    print("1. 下载frp: https://github.com/fatedier/frp/releases")
    print("2. 配置frpc.ini:")
    print(f"""
[common]
server_addr = frp.server.com
server_port = 7000

[web]
type = http
local_port = {WEB_SERVER_PORT}
custom_domains = your-domain.com
""")
    print("3. 运行: ./frpc -c frpc.ini")
    print()

def setup_cloudflare_tunnel():
    """设置Cloudflare隧道"""
    print("☁️  方案4: 使用Cloudflare隧道（免费）")
    print("-" * 30)
    print("Cloudflare提供免费的隧道服务:")
    print()
    print("📋 使用步骤:")
    print("1. 安装cloudflared:")
    print("   wget https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-linux-amd64")
    print("   chmod +x cloudflared-linux-amd64")
    print("   sudo mv cloudflared-linux-amd64 /usr/local/bin/cloudflared")
    print()
    print("2. 创建隧道:")
    print(f"   cloudflared tunnel --url http://localhost:{WEB_SERVER_PORT}")
    print()
    print("3. 复制显示的公网地址")
    print()

def setup_localtunnel():
    """设置localtunnel"""
    print("🌍 方案5: 使用localtunnel（需要Node.js）")
    print("-" * 30)
    print("localtunnel是一个简单的隧道工具:")
    print()
    print("📋 使用步骤:")
    print("1. 安装Node.js和npm")
    print("2. 安装localtunnel:")
    print("   npm install -g localtunnel")
    print()
    print("3. 启动隧道:")
    print(f"   lt --port {WEB_SERVER_PORT}")
    print()
    print("4. 复制显示的公网地址")
    print()

def create_quick_tunnel_script():
    """创建快速隧道脚本"""
    script_content = f"""#!/bin/bash

# 快速启动隧道脚本

echo "🌐 启动内网穿透隧道..."

# 检查服务器是否运行
if ! curl -s http://localhost:{WEB_SERVER_PORT}/api/health > /dev/null; then
    echo "❌ 网盘服务器未运行，请先启动服务器"
    exit 1
fi

echo "✅ 网盘服务器正在运行"

# 尝试使用不同的隧道工具
if command -v ngrok &> /dev/null; then
    echo "🚀 使用ngrok启动隧道..."
    ngrok http {WEB_SERVER_PORT}
elif command -v cloudflared &> /dev/null; then
    echo "☁️  使用cloudflared启动隧道..."
    cloudflared tunnel --url http://localhost:{WEB_SERVER_PORT}
elif command -v lt &> /dev/null; then
    echo "🌍 使用localtunnel启动隧道..."
    lt --port {WEB_SERVER_PORT}
else
    echo "❌ 未找到可用的隧道工具"
    echo "请安装以下工具之一："
    echo "- ngrok: https://ngrok.com/download"
    echo "- cloudflared: https://developers.cloudflare.com/cloudflare-one/connections/connect-apps/install-and-setup/installation/"
    echo "- localtunnel: npm install -g localtunnel"
fi
"""
    
    with open('start_tunnel.sh', 'w') as f:
        f.write(script_content)
    
    os.chmod('start_tunnel.sh', 0o755)
    print("📝 已创建快速隧道脚本: start_tunnel.sh")

def main():
    """主函数"""
    print_banner()
    
    # 显示所有方案
    setup_ngrok()
    print()
    setup_ssh_tunnel()
    print()
    setup_frp()
    print()
    setup_cloudflare_tunnel()
    print()
    setup_localtunnel()
    print()
    
    # 创建快速启动脚本
    create_quick_tunnel_script()
    
    print("=" * 50)
    print("💡 推荐方案:")
    print("1. 🥇 ngrok (最简单，有免费额度)")
    print("2. 🥈 Cloudflare隧道 (完全免费)")
    print("3. 🥉 localtunnel (简单，但不稳定)")
    print()
    print("🚀 快速启动:")
    print("   ./start_tunnel.sh")

if __name__ == '__main__':
    main()
