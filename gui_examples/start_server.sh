#!/bin/bash

# 网盘服务器启动脚本
# 支持端口80和DuckDNS域名访问

echo "🌐 启动网盘服务器..."
echo "=================================="

# 检查是否为root用户（端口80需要）
if [ "$EUID" -eq 0 ]; then
    echo "⚠️  检测到root权限，将使用端口80"
    export WEB_SERVER_PORT=80
else
    echo "ℹ️  非root用户，将使用端口8080"
    export WEB_SERVER_PORT=8080
fi

# 检查C服务器是否运行
if ! pgrep -f "./server/server" > /dev/null; then
    echo "🔧 启动C服务器..."
    cd ../server && ./server &
    sleep 2
    cd ../gui_examples
fi

# 更新DuckDNS IP（如果配置了）
if [ -f "update_duckdns.py" ]; then
    echo "🔄 更新DuckDNS IP地址..."
    python3 update_duckdns.py
fi

# 显示访问地址
echo "=================================="
echo "🌍 外网访问地址: http://tatapan.duckdns.org:$WEB_SERVER_PORT"
echo "🔗 本地访问地址: http://localhost:$WEB_SERVER_PORT"
echo "=================================="

# 启动Web服务器
echo "🚀 启动Web服务器..."
python3 app.py
