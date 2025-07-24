#!/bin/bash

# 外网访问启动脚本

echo "🌐 启动网盘外网访问..."
echo "=" * 50

# 检查网盘服务器是否运行
if ! curl -s http://localhost:8080/api/health > /dev/null 2>&1; then
    echo "❌ 网盘服务器未运行，正在启动..."
    
    # 启动C服务器
    if [ ! -f "../server/server" ]; then
        echo "❌ C服务器不存在，请先编译"
        exit 1
    fi
    
    cd ../server && ./server &
    sleep 2
    cd ../gui_examples
    
    # 启动Web服务器
    python3 app.py &
    sleep 3
    
    echo "✅ 网盘服务器已启动"
else
    echo "✅ 网盘服务器正在运行"
fi

echo ""
echo "🚀 启动Cloudflare隧道..."
echo "请等待隧道建立..."

# 启动Cloudflare隧道
./cloudflared-linux-amd64 tunnel --url http://localhost:8080
