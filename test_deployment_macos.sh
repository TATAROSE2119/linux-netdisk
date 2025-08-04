#!/bin/bash

# macOS 部署测试脚本

echo "🧪 macOS 网盘部署测试"
echo "=================================================="

# 测试编译
echo "1. 测试编译..."
if [ -f "server/server" ] && [ -f "client/client" ]; then
    echo "✅ 编译文件存在"
else
    echo "❌ 编译文件不存在，正在编译..."
    make clean && make
    if [ $? -eq 0 ]; then
        echo "✅ 编译成功"
    else
        echo "❌ 编译失败"
        exit 1
    fi
fi

# 测试目录创建
echo ""
echo "2. 测试目录结构..."
mkdir -p netdisk_data
if [ -d "netdisk_data" ]; then
    echo "✅ 数据目录创建成功"
else
    echo "❌ 数据目录创建失败"
fi

# 测试端口可用性
echo ""
echo "3. 测试端口可用性..."
if lsof -Pi :9000 -sTCP:LISTEN -t >/dev/null ; then
    echo "⚠️  端口9000已被占用"
    echo "   占用进程: $(lsof -ti:9000)"
else
    echo "✅ 端口9000可用"
fi

# 测试依赖
echo ""
echo "4. 测试系统依赖..."

# 检查SQLite
if command -v sqlite3 &> /dev/null; then
    echo "✅ SQLite3 已安装"
else
    echo "❌ SQLite3 未安装，请运行: brew install sqlite3"
fi

# 检查OpenSSL
if brew list openssl &> /dev/null; then
    echo "✅ OpenSSL 已安装"
else
    echo "❌ OpenSSL 未安装，请运行: brew install openssl"
fi

# 检查readline
if brew list readline &> /dev/null; then
    echo "✅ Readline 已安装"
else
    echo "❌ Readline 未安装，请运行: brew install readline"
fi

# 检查Python3（用于Web界面）
if command -v python3 &> /dev/null; then
    echo "✅ Python3 已安装"
    
    # 检查Flask
    python3 -c "import flask" 2>/dev/null
    if [ $? -eq 0 ]; then
        echo "✅ Flask 已安装"
    else
        echo "⚠️  Flask 未安装，请运行: pip3 install flask"
    fi

    # 检查flask-cors
    python3 -c "import flask_cors" 2>/dev/null
    if [ $? -eq 0 ]; then
        echo "✅ Flask-CORS 已安装"
    else
        echo "⚠️  Flask-CORS 未安装，请运行: pip3 install flask-cors"
    fi

    # 检查requests
    python3 -c "import requests" 2>/dev/null
    if [ $? -eq 0 ]; then
        echo "✅ Requests 已安装"
    else
        echo "⚠️  Requests 未安装，请运行: pip3 install requests"
    fi
else
    echo "❌ Python3 未安装，请运行: brew install python3"
fi

# 测试脚本权限
echo ""
echo "5. 测试启动脚本权限..."
for script in "start_server_macos.sh" "start_client_macos.sh" "gui_examples/start_server_macos.sh"; do
    if [ -x "$script" ]; then
        echo "✅ $script 有执行权限"
    else
        echo "⚠️  $script 没有执行权限，正在修复..."
        chmod +x "$script"
    fi
done

echo ""
echo "=================================================="
echo "🎉 测试完成！"
echo ""
echo "📋 下一步操作："
echo "1. 启动服务器: ./start_server_macos.sh"
echo "2. 启动客户端: ./start_client_macos.sh"
echo "3. 启动Web界面: cd gui_examples && ./start_server_macos.sh"
echo ""
echo "📖 详细说明请查看: README_macOS.md"
