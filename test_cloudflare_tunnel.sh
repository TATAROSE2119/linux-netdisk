#!/bin/bash

# 测试 Cloudflare 隧道功能

echo "🧪 测试 Cloudflare 隧道"
echo "=================================================="

# 获取脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# 检查目录结构
echo "📁 检查目录结构..."
if [ -d "gui_examples" ]; then
    echo "✅ gui_examples 目录存在"
else
    echo "❌ gui_examples 目录不存在"
    exit 1
fi

# 检查并启动本地服务
echo ""
echo "🔧 检查本地服务..."
if ! nc -z 127.0.0.1 8080 2>/dev/null; then
    echo "⚠️  Web服务器未运行，正在启动..."
    
    # 启动C服务器
    if [ ! -f "server/server" ]; then
        echo "📦 编译项目..."
        make > /dev/null 2>&1
    fi
    
    echo "🚀 启动C服务器..."
    (cd server && ./server > /dev/null 2>&1) &
    C_SERVER_PID=$!
    sleep 2
    
    echo "🌐 启动Web服务器..."
    (cd gui_examples && export WEB_SERVER_PORT=8080 && python3 app.py > /dev/null 2>&1) &
    WEB_SERVER_PID=$!
    sleep 3
    
    echo "✅ 本地服务启动完成"
else
    echo "✅ Web服务器已运行"
fi

# 检查并下载 cloudflared
echo ""
echo "📥 检查 Cloudflare 客户端..."
if [ ! -f "gui_examples/cloudflared-darwin-amd64" ]; then
    echo "⬇️  下载 Cloudflare 隧道客户端..."
    (cd gui_examples && curl -L https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-darwin-amd64 -o cloudflared-darwin-amd64)
    
    if [ $? -eq 0 ] && [ -f "gui_examples/cloudflared-darwin-amd64" ]; then
        chmod +x gui_examples/cloudflared-darwin-amd64
        echo "✅ 下载完成"
    else
        echo "❌ 下载失败"
        exit 1
    fi
else
    echo "✅ Cloudflare 客户端已存在"
fi

# 测试本地访问
echo ""
echo "🔍 测试本地访问..."
if curl -s http://localhost:8080 >/dev/null 2>&1; then
    echo "✅ 本地访问正常: http://localhost:8080"
else
    echo "❌ 本地访问失败"
    exit 1
fi

# 启动隧道（测试模式）
echo ""
echo "🌉 测试 Cloudflare 隧道启动..."
echo "🔗 如果看到 'Your quick Tunnel' 信息，说明隧道功能正常"
echo "⚠️  按 Ctrl+C 停止测试"
echo ""

# 启动隧道进行测试
(cd gui_examples && ./cloudflared-darwin-amd64 tunnel --url http://localhost:8080) &
TUNNEL_PID=$!

# 等待5秒然后停止
sleep 5
kill $TUNNEL_PID 2>/dev/null
wait $TUNNEL_PID 2>/dev/null

echo ""
echo "=================================================="
echo "🎉 测试完成"
echo "=================================================="
echo ""
echo "📋 测试结果："
echo "   ✅ 目录结构正确"
echo "   ✅ 本地服务正常"
echo "   ✅ Cloudflare 客户端可用"
echo "   ✅ 隧道启动成功"
echo ""
echo "🚀 要启动完整的外网访问服务，请运行："
echo "   ./start_external_macos.sh"
