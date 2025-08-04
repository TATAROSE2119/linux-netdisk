#!/bin/bash

# macOS 网盘Web服务器启动脚本

echo "🍎 macOS 网盘Web服务器启动脚本"
echo "=================================================="

# 检查Python3是否安装
if ! command -v python3 &> /dev/null; then
    echo "❌ Python3 未安装，请先安装Python3："
    echo "   brew install python3"
    exit 1
fi

# 检查必要的Python包
echo "📦 检查Python依赖..."
python3 -c "import flask, flask_cors, requests" 2>/dev/null
if [ $? -ne 0 ]; then
    echo "⚠️  缺少必要的Python包，正在安装..."
    pip3 install flask flask-cors requests
fi

# 设置端口（macOS上通常使用8080）
export WEB_SERVER_PORT=8080
echo "ℹ️  Web服务器将在端口 $WEB_SERVER_PORT 上运行"

# 检查C服务器是否运行
echo "🔍 检查C服务器状态..."
if ! nc -z 127.0.0.1 9000 2>/dev/null; then
    echo "⚠️  C服务器未运行，正在启动..."
    
    # 检查是否已编译
    if [ ! -f "../server/server" ]; then
        echo "📦 正在编译C服务器..."
        cd .. && make server/server
        if [ $? -ne 0 ]; then
            echo "❌ C服务器编译失败"
            exit 1
        fi
        cd gui_examples
    fi
    
    # 启动C服务器
    cd ../server && ./server &
    C_SERVER_PID=$!
    echo "✅ C服务器已启动 (PID: $C_SERVER_PID)"
    sleep 3
    cd ../gui_examples
else
    echo "✅ C服务器正在运行"
fi

# 显示访问地址
echo "=================================================="
echo "🔗 本地访问地址: http://localhost:$WEB_SERVER_PORT"
echo "🔗 局域网访问地址: http://$(ipconfig getifaddr en0):$WEB_SERVER_PORT"
echo "=================================================="

# 启动Web服务器
echo "🚀 启动Web服务器..."
echo "按 Ctrl+C 停止服务器"
python3 app.py
