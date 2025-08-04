#!/bin/bash

# macOS 一键启动网盘服务端和Web客户端脚本

echo "🍎 macOS 网盘一键启动脚本"
echo "=================================================="

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 日志函数
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
    exit 0
}

# 设置信号处理
trap cleanup SIGINT SIGTERM

# 检查依赖
check_dependencies() {
    log_info "检查系统依赖..."
    
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
    
    # 检查必要的Python包
    log_info "检查Python包依赖..."
    python3 -c "import flask, flask_cors, requests" 2>/dev/null
    if [ $? -ne 0 ]; then
        log_warning "缺少Python依赖，正在安装..."
        pip3 install flask flask-cors requests
        if [ $? -ne 0 ]; then
            log_error "Python依赖安装失败，请手动安装："
            log_error "pip3 install flask flask-cors requests"
            exit 1
        fi
        log_success "Python依赖安装完成"
    else
        log_success "Python依赖检查通过"
    fi
    
    log_success "依赖检查完成"
}

# 编译项目
compile_project() {
    log_info "检查编译状态..."
    
    if [ ! -f "server/server" ] || [ ! -f "client/client" ]; then
        log_info "正在编译项目..."
        make clean && make
        if [ $? -ne 0 ]; then
            log_error "编译失败"
            exit 1
        fi
        log_success "编译完成"
    else
        log_success "项目已编译"
    fi
}

# 创建必要目录
create_directories() {
    log_info "创建必要目录..."
    mkdir -p netdisk_data
    log_success "目录创建完成"
}

# 检查端口
check_ports() {
    log_info "检查端口占用情况..."
    
    # 检查C服务器端口 (9000)
    if lsof -Pi :9000 -sTCP:LISTEN -t >/dev/null 2>&1; then
        log_warning "端口9000已被占用，正在尝试释放..."
        lsof -ti:9000 | xargs kill -9 2>/dev/null
        sleep 2
    fi
    
    # 检查Web服务器端口 (8080)
    if lsof -Pi :8080 -sTCP:LISTEN -t >/dev/null 2>&1; then
        log_warning "端口8080已被占用，正在尝试释放..."
        lsof -ti:8080 | xargs kill -9 2>/dev/null
        sleep 2
    fi
    
    log_success "端口检查完成"
}

# 启动C服务器
start_c_server() {
    log_info "启动C服务器..."
    
    cd server
    ./server &
    C_SERVER_PID=$!
    cd ..
    
    # 等待服务器启动
    sleep 3
    
    # 检查服务器是否成功启动
    if nc -z 127.0.0.1 9000 2>/dev/null; then
        log_success "C服务器启动成功 (PID: $C_SERVER_PID, 端口: 9000)"
    else
        log_error "C服务器启动失败"
        exit 1
    fi
}

# 启动Web服务器
start_web_server() {
    log_info "启动Web服务器..."
    
    cd gui_examples
    export WEB_SERVER_PORT=8080
    python3 app.py &
    WEB_SERVER_PID=$!
    cd ..
    
    # 等待Web服务器启动
    sleep 5
    
    # 检查Web服务器是否成功启动
    if curl -s http://localhost:8080 >/dev/null 2>&1; then
        log_success "Web服务器启动成功 (PID: $WEB_SERVER_PID, 端口: 8080)"
    else
        log_warning "Web服务器可能需要更多时间启动"
    fi
}

# 显示访问信息
show_access_info() {
    echo ""
    echo "=================================================="
    echo -e "${GREEN}🎉 网盘服务启动完成！${NC}"
    echo "=================================================="
    echo ""
    echo -e "${BLUE}📱 访问地址:${NC}"
    echo "   🔗 Web界面: http://localhost:8080"
    echo "   🔗 局域网访问: http://$(ipconfig getifaddr en0 2>/dev/null || echo "获取IP失败"):8080"
    echo ""
    echo -e "${BLUE}🖥️  服务信息:${NC}"
    echo "   📡 C服务器: 127.0.0.1:9000 (PID: $C_SERVER_PID)"
    echo "   🌐 Web服务器: 0.0.0.0:8080 (PID: $WEB_SERVER_PID)"
    echo ""
    echo -e "${BLUE}💡 使用说明:${NC}"
    echo "   1. 在Web界面中注册/登录账户"
    echo "   2. 上传、下载、管理文件"
    echo "   3. 按 Ctrl+C 停止所有服务"
    echo ""
    echo -e "${BLUE}🔧 命令行客户端:${NC}"
    echo "   如需使用命令行客户端，请在新终端运行:"
    echo "   ./start_client_macos.sh"
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
    create_directories
    check_ports
    start_c_server
    start_web_server
    show_access_info
    wait_for_interrupt
}

# 运行主函数
main
