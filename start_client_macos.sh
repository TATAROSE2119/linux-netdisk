#!/bin/bash

# macOS 网盘客户端启动脚本

echo "🍎 macOS 网盘客户端启动脚本"
echo "=================================================="

# 检查是否已编译
if [ ! -f "client/client" ]; then
    echo "📦 正在编译客户端..."
    make client/client
    if [ $? -ne 0 ]; then
        echo "❌ 编译失败，请检查错误信息"
        echo "提示：请确保已安装 readline 库："
        echo "brew install readline"
        exit 1
    fi
    echo "✅ 编译完成"
fi

# 检查服务器是否运行
echo "🔍 检查服务器状态..."
if ! nc -z 127.0.0.1 9000 2>/dev/null; then
    echo "⚠️  服务器未运行，请先启动服务器："
    echo "   ./start_server_macos.sh"
    echo ""
    echo "或者在另一个终端窗口中运行："
    echo "   cd server && ./server"
    echo ""
    read -p "是否继续启动客户端？(y/N): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
else
    echo "✅ 服务器正在运行"
fi

echo ""
echo "🚀 启动网盘客户端..."
echo "连接到服务器: 127.0.0.1:9000"
echo "=================================================="

# 启动客户端
cd client && ./client
