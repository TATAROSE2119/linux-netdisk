#!/bin/bash

# 停止网盘服务脚本

echo "🛑 停止网盘服务..."
echo "=================================================="

# 颜色定义
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info() {
    echo -e "${BLUE}ℹ️  $1${NC}"
}

log_success() {
    echo -e "${GREEN}✅ $1${NC}"
}

# 停止C服务器 (端口9000)
log_info "停止C服务器..."
C_SERVER_PIDS=$(lsof -ti:9000 2>/dev/null)
if [ ! -z "$C_SERVER_PIDS" ]; then
    echo $C_SERVER_PIDS | xargs kill -9 2>/dev/null
    log_success "C服务器已停止"
else
    log_info "C服务器未运行"
fi

# 停止Web服务器 (端口8080)
log_info "停止Web服务器..."
WEB_SERVER_PIDS=$(lsof -ti:8080 2>/dev/null)
if [ ! -z "$WEB_SERVER_PIDS" ]; then
    echo $WEB_SERVER_PIDS | xargs kill -9 2>/dev/null
    log_success "Web服务器已停止"
else
    log_info "Web服务器未运行"
fi

# 清理Python进程
pkill -f "python3 app.py" 2>/dev/null
pkill -f "server/server" 2>/dev/null

# 等待进程完全停止
sleep 2

echo ""
echo "=================================================="
echo -e "${GREEN}🎉 所有服务已停止${NC}"
echo "=================================================="
