#!/bin/bash

# 快速启动网盘服务（简化版）

# 获取脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "🚀 快速启动网盘服务..."

# 检查Python依赖
python3 -c "import flask, flask_cors, requests" 2>/dev/null
if [ $? -ne 0 ]; then
    echo "📦 安装Python依赖..."
    pip3 install flask flask-cors requests > /dev/null 2>&1
fi

# 检查并编译
if [ ! -f "server/server" ]; then
    echo "📦 编译中..."
    make > /dev/null 2>&1
fi

# 创建目录
mkdir -p netdisk_data

# 清理端口
lsof -ti:9000 | xargs kill -9 2>/dev/null
lsof -ti:8080 | xargs kill -9 2>/dev/null
sleep 1

# 启动服务
echo "🔧 启动C服务器..."
(cd server && ./server > /dev/null 2>&1) &
sleep 2

echo "🌐 启动Web服务器..."
if [ -d "gui_examples" ]; then
    (cd gui_examples && export WEB_SERVER_PORT=8080 && python3 app.py > /dev/null 2>&1) &
    sleep 3
else
    echo "❌ gui_examples 目录不存在"
    echo "当前目录: $(pwd)"
    echo "目录内容: $(ls -la)"
    exit 1
fi

# 检查状态
if nc -z 127.0.0.1 9000 2>/dev/null && curl -s http://localhost:8080 >/dev/null 2>&1; then
    echo "✅ 启动成功！"
    echo ""
    echo "🔗 Web访问: http://localhost:8080"
    echo "🛑 停止服务: ./stop_all_macos.sh"
    echo ""
    echo "按回车键退出..."
    read
else
    echo "❌ 启动失败，请使用详细版本: ./start_all_macos.sh"
fi
