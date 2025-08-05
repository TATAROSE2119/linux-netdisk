��盘服务一键启动脚本

echo "🌐 网盘服务一键启动"
echo "=================================================="

# 颜色定义
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
RED='\033[0;31m'
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
    log_info "正在停止服务..."
    ./stop.sh > /dev/null 2>&1
    exit 0
}

trap cleanup SIGINT SIGTERM

# 检查依赖
check_dependencies() {
    log_info "检查依赖..."
    
    # 检查编译工具
    if ! command -v gcc &> /dev/null; then
        log_error "GCC 未安装，请运行: xcode-select --install"
        exit 1
    fi
    
    # 检查Python3
    if ! command -v python3 &> /dev/null; then
        log_error "Python3 未安装，请运行: brew install python3"
        exit 1
    fi
    
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
    log_info "编译项目..."
    
    if [ ! -f "server/server" ] || [ ! -f "client/client" ]; then
        make clean && bear -- make
        if [ $? -ne 0 ]; then
            log_error "编译失败"
            exit 1
        fi
    fi
    
    log_success "编译完成"
}

# 启动服务
start_services() {
    log_info "启动服务..."
    
    # 创建数据目录
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
    
    if ! nc -z 127.0.0.1 9000 2>/dev/null; then
        log_error "C服务器启动失败"
        exit 1
    fi
    log_success "C服务器启动成功 (PID: $C_SERVER_PID)"
    
    # 启动Web服务器
    log_info "启动Web服务器..."
    (cd gui_examples && export WEB_SERVER_PORT=8080 && python3 app.py > /dev/null 2>&1) &
    WEB_SERVER_PID=$!
    sleep 3
    
    if curl -s http://localhost:8080 >/dev/null 2>&1; then
        log_success "Web服务器启动成功 (PID: $WEB_SERVER_PID)"
    else
        log_warning "Web服务器可能需要更多时间启动"
    fi
}

# 显示访问信息
show_access_info() {
    LOCAL_IP=$(ipconfig getifaddr en0 2>/dev/null || ipconfig getifaddr en1 2>/dev/null)
    
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
    echo -e "${BLUE}🌐 外网访问 (使用 Ngrok):${NC}"
    echo "   1. 安装 Ngrok: brew install ngrok/ngrok/ngrok"
    echo "   2. 注册账号: https://ngrok.com/"
    echo "   3. 配置令牌: ngrok config add-authtoken <your-token>"
    echo "   4. 启动隧道: ngrok http 8080"
    echo "   5. 使用 Ngrok 提供的 URL 访问"
    echo ""
    echo -e "${BLUE}💡 使用说明:${NC}"
    echo "   1. 在Web界面中注册/登录账户"
    echo "   2. 上传、下载、管理文件"
    echo "   3. 按 Ctrl+C 停止服务"
    echo ""
    echo -e "${BLUE}🖥️  命令行客户端:${NC}"
    echo "   在新终端运行: cd client && ./client"
    echo ""
    echo "=================================================="
}

# 等待用户中断
wait_for_interrupt() {
    log_info "服务正在运行中... 按 Ctrl+C 停止"
    
    while true; do
        sleep 5
        
        # 检查服务状态
        if ! kill -0 $C_SERVER_PID 2>/dev/null; then
            log_error "C服务器意外停止"
            cleanup
        fi
        
        if ! kill -0 $WEB_SERVER_PID 2>/dev/null; then
            log_error "Web服务器意外停止"
            cleanup
        fi
    done
}

# 主函数
main() {
    check_dependencies
    compile_project
    start_services
    show_access_info
    wait_for_interrupt
}

# 运行主函数
main
