#!/bin/bash

# macOS 网盘服务器启动脚本

echo "🍎 macOS 网盘服务器启动脚本"
echo "=================================================="

# 检查是否已编译
if [ ! -f "server/server" ] || [ ! -f "client/client" ]; then
    echo "📦 正在编译项目..."
    make clean
    make
    if [ $? -ne 0 ]; then
        echo "❌ 编译失败，请检查错误信息"
        exit 1
    fi
    echo "✅ 编译完成"
fi

# 创建必要的目录
echo "📁 创建必要的目录..."
mkdir -p netdisk_data
echo "✅ 目录创建完成"

# 检查端口是否被占用
PORT=9000
if lsof -Pi :$PORT -sTCP:LISTEN -t >/dev/null ; then
    echo "⚠️  端口 $PORT 已被占用，正在尝试终止占用进程..."
    lsof -ti:$PORT | xargs kill -9
    sleep 2
fi

# 启动服务器
echo "🚀 启动网盘服务器..."
echo "服务器将在端口 $PORT 上监听"
echo "按 Ctrl+C 停止服务器"
echo "=================================================="

# 启动服务器并显示输出
cd server && ./server
