#!/bin/bash

# 外网访问启动脚本 - macOS版本

echo "🌐 启动网盘外网访问..."
echo "=================================================="

# 检测操作系统
OS=$(uname -s)
echo "检测到操作系统: $OS"

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

# 根据操作系统选择合适的cloudflared二进制文件
if [ "$OS" = "Darwin" ]; then
    # macOS
    if [ ! -f "./cloudflared-darwin-amd64" ]; then
        echo "❌ 未找到macOS版本的cloudflared，正在下载..."
        curl -L https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-darwin-amd64 -o cloudflared-darwin-amd64
        chmod +x cloudflared-darwin-amd64
    fi

    # 检查服务是否运行
    if ! nc -z localhost 8080 2>/dev/null; then
        echo "⚠️  Web服务器未运行，正在启动..."
        # 启动C服务器
        if [ ! -f "../server/server" ]; then
            echo "❌ C服务器不存在，请先编译"
            exit 1
        fi
        cd ../server && ./server > /dev/null 2>&1 &
        sleep 2
        cd ../gui_examples

        # 启动Web服务器
        export WEB_SERVER_PORT=8080
        python3 app.py > /dev/null 2>&1 &
        sleep 3
    fi

    ./cloudflared-darwin-amd64 tunnel --url http://localhost:8080
elif [ "$OS" = "Linux" ]; then
    # Linux
    if [ ! -f "./cloudflared-linux-amd64" ]; then
        echo "❌ 未找到Linux版本的cloudflared，正在下载..."
        curl -L https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-linux-amd64 -o cloudflared-linux-amd64
        chmod +x cloudflared-linux-amd64
    fi
    ./cloudflared-linux-amd64 tunnel --url http://localhost:8080
else
    echo "❌ 不支持的操作系统: $OS"
    exit 1
fi
