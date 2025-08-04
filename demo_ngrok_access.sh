#!/bin/bash

# Ngrok 外网访问演示脚本

echo "🔗 Ngrok 外网访问演示"
echo "=================================================="

# 颜色定义
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info() {
    echo -e "${BLUE}ℹ️  $1${NC}"
}

log_success() {
    echo -e "${GREEN}✅ $1${NC}"
}

log_warning() {
    echo -e "${YELLOW}⚠️  $1${NC}"
}

# 检查 Ngrok 是否安装
check_ngrok() {
    if command -v ngrok &> /dev/null; then
        log_success "Ngrok 已安装"
        return 0
    else
        log_warning "Ngrok 未安装"
        echo ""
        echo "📦 安装 Ngrok："
        echo "   brew install ngrok/ngrok/ngrok"
        echo ""
        echo "🔑 注册并获取认证令牌："
        echo "   1. 访问 https://ngrok.com/"
        echo "   2. 注册免费账号"
        echo "   3. 获取认证令牌"
        echo "   4. 运行: ngrok config add-authtoken <your-token>"
        echo ""
        return 1
    fi
}

# 启动本地服务
start_local_service() {
    log_info "启动本地网盘服务..."
    
    # 检查服务是否已运行
    if nc -z 127.0.0.1 8080 2>/dev/null; then
        log_success "本地服务已运行"
        return 0
    fi
    
    # 启动服务
    ./start_external_simple.sh > /dev/null 2>&1 &
    LOCAL_SERVICE_PID=$!
    
    # 等待服务启动
    log_info "等待服务启动..."
    for i in {1..10}; do
        if nc -z 127.0.0.1 8080 2>/dev/null; then
            log_success "本地服务启动成功"
            return 0
        fi
        sleep 1
    done
    
    log_warning "本地服务启动超时，请手动检查"
    return 1
}

# 显示使用说明
show_instructions() {
    echo ""
    echo "=================================================="
    echo -e "${GREEN}🎉 准备就绪！${NC}"
    echo "=================================================="
    echo ""
    echo -e "${BLUE}📋 下一步操作：${NC}"
    echo ""
    echo "1️⃣ 在新终端窗口运行："
    echo "   ngrok http 8080"
    echo ""
    echo "2️⃣ Ngrok 会显示类似这样的信息："
    echo "   Forwarding  https://abc123.ngrok.io -> http://localhost:8080"
    echo ""
    echo "3️⃣ 使用外网地址访问："
    echo "   https://abc123.ngrok.io"
    echo ""
    echo "4️⃣ 在网盘界面中："
    echo "   • 注册新账户或登录"
    echo "   • 上传、下载文件"
    echo "   • 管理文件和目录"
    echo ""
    echo -e "${YELLOW}⚠️  注意事项：${NC}"
    echo "   • 免费版 Ngrok 每次重启 URL 会变化"
    echo "   • 使用完毕请及时关闭隧道"
    echo "   • 建议设置强密码保护账户"
    echo ""
    echo "=================================================="
}

# 等待用户操作
wait_for_user() {
    echo ""
    echo "🔗 本地服务地址: http://localhost:8080"
    echo "🏠 局域网地址: http://$(ipconfig getifaddr en0 2>/dev/null || echo "获取失败"):8080"
    echo ""
    echo "按回车键停止本地服务..."
    read -r
}

# 清理函数
cleanup() {
    log_info "正在停止服务..."
    ./stop_all_macos.sh > /dev/null 2>&1
    if [ ! -z "$LOCAL_SERVICE_PID" ]; then
        kill $LOCAL_SERVICE_PID 2>/dev/null
    fi
    log_success "服务已停止"
}

# 主函数
main() {
    # 检查是否在正确目录
    if [ ! -f "start_external_simple.sh" ]; then
        echo "❌ 请在项目根目录运行此脚本"
        exit 1
    fi
    
    # 检查 Ngrok
    if ! check_ngrok; then
        echo ""
        echo "请先安装并配置 Ngrok，然后重新运行此脚本"
        exit 1
    fi
    
    # 启动本地服务
    if start_local_service; then
        show_instructions
        wait_for_user
        cleanup
    else
        echo "❌ 本地服务启动失败"
        exit 1
    fi
}

# 设置信号处理
trap cleanup SIGINT SIGTERM

# 运行主函数
main
