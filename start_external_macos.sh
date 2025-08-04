#!/bin/bash

# macOS 一键启动外网访问脚本

# 获取脚本所在目录并切换到项目根目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "🌐 macOS 网盘外网访问一键启动"
echo "=================================================="

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

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
    if [ ! -z "$TUNNEL_PID" ]; then
        kill $TUNNEL_PID 2>/dev/null
        log_info "已终止隧道服务 (PID: $TUNNEL_PID)"
    fi
    exit 0
}

# 设置信号处理
trap cleanup SIGINT SIGTERM

# 检查并安装依赖
check_dependencies() {
    log_info "检查依赖..."
    
    # 检查Python依赖
    python3 -c "import flask, flask_cors, requests" 2>/dev/null
    if [ $? -ne 0 ]; then
        log_warning "安装Python依赖..."
        pip3 install flask flask-cors requests
    fi
    
    log_success "依赖检查完成"
}

# 编译项目
compile_project() {
    log_info "检查编译状态..."
    
    if [ ! -f "server/server" ] || [ ! -f "client/client" ]; then
        log_info "编译项目..."
        make clean && make
        if [ $? -ne 0 ]; then
            log_error "编译失败"
            exit 1
        fi
    fi
    
    log_success "项目编译完成"
}

# 启动本地服务
start_local_services() {
    log_info "启动本地服务..."
    
    # 清理端口
    lsof -ti:9000 | xargs kill -9 2>/dev/null
    lsof -ti:8080 | xargs kill -9 2>/dev/null
    sleep 1
    
    # 创建目录
    mkdir -p netdisk_data
    
    # 启动C服务器
    log_info "启动C服务器..."
    cd server && ./server > /dev/null 2>&1 &
    C_SERVER_PID=$!
    cd ..
    sleep 2
    
    # 检查C服务器
    if ! nc -z 127.0.0.1 9000 2>/dev/null; then
        log_error "C服务器启动失败"
        exit 1
    fi
    log_success "C服务器启动成功 (PID: $C_SERVER_PID)"
    
    # 启动Web服务器
    log_info "启动Web服务器..."
    if [ -d "gui_examples" ]; then
        (cd gui_examples && export WEB_SERVER_PORT=8080 && python3 app.py > /dev/null 2>&1) &
        WEB_SERVER_PID=$!
        sleep 3
    else
        log_error "gui_examples 目录不存在"
        exit 1
    fi
    
    # 检查Web服务器
    if ! curl -s http://localhost:8080 >/dev/null 2>&1; then
        log_warning "Web服务器可能需要更多时间启动"
    else
        log_success "Web服务器启动成功 (PID: $WEB_SERVER_PID)"
    fi
}

# 启动Cloudflare隧道
start_cloudflare_tunnel() {
    log_info "启动Cloudflare隧道..."
    
    # 检查并下载cloudflared
    if [ ! -f "gui_examples/cloudflared-darwin-amd64" ]; then
        log_info "下载Cloudflare隧道客户端..."
        if [ -d "gui_examples" ]; then
            (cd gui_examples && curl -L https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-darwin-amd64 -o cloudflared-darwin-amd64 && chmod +x cloudflared-darwin-amd64)
            if [ $? -eq 0 ]; then
                log_success "下载完成"
            else
                log_error "下载失败"
                exit 1
            fi
        else
            log_error "gui_examples 目录不存在"
            exit 1
        fi
    fi

    # 启动隧道
    if [ -f "gui_examples/cloudflared-darwin-amd64" ]; then
        (cd gui_examples && ./cloudflared-darwin-amd64 tunnel --url http://localhost:8080) &
        TUNNEL_PID=$!
    else
        log_error "cloudflared 客户端不存在"
        exit 1
    fi
    
    log_success "Cloudflare隧道启动成功 (PID: $TUNNEL_PID)"
    
    # 等待隧道建立
    log_info "等待隧道建立..."
    sleep 5
}

# 显示访问信息
show_access_info() {
    echo ""
    echo "=================================================="
    echo -e "${GREEN}🎉 外网访问服务启动完成！${NC}"
    echo "=================================================="
    echo ""
    echo -e "${BLUE}📱 访问方式:${NC}"
    echo "   🔗 本地访问: http://localhost:8080"
    
    # 获取局域网IP
    LOCAL_IP=$(ipconfig getifaddr en0 2>/dev/null)
    if [ ! -z "$LOCAL_IP" ]; then
        echo "   🏠 局域网访问: http://$LOCAL_IP:8080"
    fi
    
    echo "   🌐 外网访问: 查看上方Cloudflare隧道输出的URL"
    echo ""
    echo -e "${BLUE}🖥️  服务信息:${NC}"
    echo "   📡 C服务器: 127.0.0.1:9000 (PID: $C_SERVER_PID)"
    echo "   🌐 Web服务器: 0.0.0.0:8080 (PID: $WEB_SERVER_PID)"
    echo "   🌉 Cloudflare隧道: (PID: $TUNNEL_PID)"
    echo ""
    echo -e "${BLUE}💡 使用说明:${NC}"
    echo "   1. 使用上方显示的外网URL访问网盘"
    echo "   2. 在Web界面中注册/登录账户"
    echo "   3. 上传、下载、管理文件"
    echo "   4. 按 Ctrl+C 停止所有服务"
    echo ""
    echo -e "${YELLOW}⚠️  注意事项:${NC}"
    echo "   • Cloudflare隧道提供的是临时URL"
    echo "   • 隧道重启后URL会改变"
    echo "   • 适合临时外网访问使用"
    echo ""
    echo "=================================================="
}

# 等待用户中断
wait_for_interrupt() {
    log_info "服务正在运行中... 按 Ctrl+C 停止"
    
    # 持续监控服务状态
    while true; do
        sleep 10
        
        # 检查C服务器状态
        if ! kill -0 $C_SERVER_PID 2>/dev/null; then
            log_error "C服务器意外停止"
            cleanup
        fi
        
        # 检查Web服务器状态
        if ! kill -0 $WEB_SERVER_PID 2>/dev/null; then
            log_error "Web服务器意外停止"
            cleanup
        fi
        
        # 检查隧道状态
        if ! kill -0 $TUNNEL_PID 2>/dev/null; then
            log_error "Cloudflare隧道意外停止"
            cleanup
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
    
    # 执行启动流程
    check_dependencies
    compile_project
    start_local_services
    start_cloudflare_tunnel
    show_access_info
    wait_for_interrupt
}

# 运行主函数
main
