#!/bin/bash

# 简化版外网访问启动脚本

echo "🌐 简化版外网访问启动"
echo "=================================================="

# 获取脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# 颜色定义
RED='\033[0;31m'
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

log_error() {
    echo -e "${RED}❌ $1${NC}"
}

# 清理函数
cleanup() {
    log_info "正在清理进程..."
    if [ ! -z "$C_SERVER_PID" ]; then
        kill $C_SERVER_PID 2>/dev/null
        log_info "已终止C服务器 (PID: $C_SERVER_PID)"
    fi
    if [ ! -z "$WEB_SERVER_PID" ]; then
        kill $WEB_SERVER_PID 2>/dev/null
        log_info "已终止Web服务器 (PID: $WEB_SERVER_PID)"
    fi
    ./stop_all_macos.sh > /dev/null 2>&1
    exit 0
}

trap cleanup SIGINT SIGTERM

# 获取本机IP
get_local_ip() {
    LOCAL_IP=$(ipconfig getifaddr en0 2>/dev/null)
    if [ -z "$LOCAL_IP" ]; then
        LOCAL_IP=$(ipconfig getifaddr en1 2>/dev/null)
    fi
    echo "$LOCAL_IP"
}

# 获取公网IP
get_public_ip() {
    PUBLIC_IP=$(curl -s --connect-timeout 5 https://api.ipify.org 2>/dev/null)
    if [ -z "$PUBLIC_IP" ]; then
        PUBLIC_IP=$(curl -s --connect-timeout 5 https://icanhazip.com 2>/dev/null)
    fi
    echo "$PUBLIC_IP"
}

# 启动本地服务
start_local_services() {
    log_info "启动本地服务..."

    # 检查并编译
    if [ ! -f "server/server" ]; then
        log_info "编译项目..."
        make > /dev/null 2>&1
        if [ $? -ne 0 ]; then
            log_error "编译失败"
            return 1
        fi
    fi

    # 停止现有服务
    ./stop_all_macos.sh > /dev/null 2>&1
    sleep 1

    # 创建必要目录
    mkdir -p netdisk_data

    # 清理端口
    lsof -ti:9000 | xargs kill -9 2>/dev/null
    lsof -ti:8080 | xargs kill -9 2>/dev/null
    sleep 1

    # 启动C服务器
    log_info "启动C服务器..."
    (cd server && ./server > /dev/null 2>&1) &
    C_SERVER_PID=$!
    sleep 2

    # 检查C服务器
    if ! nc -z 127.0.0.1 9000 2>/dev/null; then
        log_error "C服务器启动失败"
        return 1
    fi
    log_success "C服务器启动成功 (PID: $C_SERVER_PID)"

    # 启动Web服务器
    log_info "启动Web服务器..."
    if [ -d "gui_examples" ]; then
        (cd gui_examples && export WEB_SERVER_PORT=8080 && python3 app.py > /dev/null 2>&1) &
        WEB_SERVER_PID=$!
        sleep 3

        # 检查Web服务器
        if curl -s http://localhost:8080 >/dev/null 2>&1; then
            log_success "Web服务器启动成功 (PID: $WEB_SERVER_PID)"
            return 0
        else
            log_warning "Web服务器可能需要更多时间启动"
            sleep 2
            if curl -s http://localhost:8080 >/dev/null 2>&1; then
                log_success "Web服务器启动成功 (PID: $WEB_SERVER_PID)"
                return 0
            else
                log_error "Web服务器启动失败"
                return 1
            fi
        fi
    else
        log_error "gui_examples 目录不存在"
        return 1
    fi
}

# 显示访问信息
show_access_info() {
    LOCAL_IP=$(get_local_ip)
    PUBLIC_IP=$(get_public_ip)
    
    echo ""
    echo "=================================================="
    echo -e "${GREEN}🎉 网盘服务启动完成！${NC}"
    echo "=================================================="
    echo ""
    echo -e "${BLUE}📱 访问地址:${NC}"
    echo "   🔗 本地访问: http://localhost:8080"
    
    if [ ! -z "$LOCAL_IP" ]; then
        echo "   🏠 局域网访问: http://$LOCAL_IP:8080"
    fi
    
    echo ""
    echo -e "${BLUE}🌐 外网访问方案:${NC}"
    echo ""
    
    # 方案1: Ngrok
    if command -v ngrok &> /dev/null; then
        echo "   1️⃣ Ngrok 隧道 (推荐)"
        echo "      在新终端运行: ngrok http 8080"
        echo "      然后使用 Ngrok 提供的 URL 访问"
        echo ""
    else
        echo "   1️⃣ 安装 Ngrok (推荐)"
        echo "      brew install ngrok/ngrok/ngrok"
        echo "      ngrok http 8080"
        echo ""
    fi
    
    # 方案2: 路由器端口转发
    if [ ! -z "$PUBLIC_IP" ]; then
        echo "   2️⃣ 路由器端口转发"
        echo "      配置路由器转发端口 8080 到 $LOCAL_IP:8080"
        echo "      外网访问: http://$PUBLIC_IP:8080"
        echo ""
    fi
    
    # 方案3: Cloudflare隧道
    echo "   3️⃣ Cloudflare 隧道"
    echo "      cd gui_examples"
    echo "      ./start_external_access.sh"
    echo ""
    
    # 方案4: 其他隧道服务
    echo "   4️⃣ 其他隧道服务"
    echo "      • LocalTunnel: npx localtunnel --port 8080"
    echo "      • Serveo: ssh -R 80:localhost:8080 serveo.net"
    echo ""
    
    echo -e "${BLUE}💡 使用说明:${NC}"
    echo "   1. 选择上述任一外网访问方案"
    echo "   2. 在Web界面中注册/登录账户"
    echo "   3. 上传、下载、管理文件"
    echo "   4. 按 Ctrl+C 停止服务"
    echo ""
    
    echo -e "${YELLOW}⚠️  安全提醒:${NC}"
    echo "   • 外网访问存在安全风险，请谨慎使用"
    echo "   • 建议使用强密码并及时关闭外网访问"
    echo "   • 不要在公共网络环境下使用"
    echo ""
    echo "=================================================="
}

# 等待用户中断
wait_for_interrupt() {
    log_info "服务正在运行中... 按 Ctrl+C 停止"
    
    while true; do
        sleep 5
        
        # 检查服务状态
        if ! nc -z 127.0.0.1 9000 2>/dev/null || ! nc -z 127.0.0.1 8080 2>/dev/null; then
            log_error "服务意外停止"
            break
        fi
    done
}

# 主函数
main() {
    # 检查是否在正确的目录
    if [ ! -f "Makefile" ] || [ ! -d "server" ] || [ ! -d "client" ]; then
        log_error "请在项目根目录运行此脚本"
        exit 1
    fi
    
    # 启动本地服务
    if start_local_services; then
        show_access_info
        wait_for_interrupt
    else
        log_error "启动失败"
        exit 1
    fi
}

# 运行主函数
main
