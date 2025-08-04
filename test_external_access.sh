#!/bin/bash

# 测试外网访问功能

echo "🧪 测试外网访问功能"
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

# 获取网络信息
get_network_info() {
    log_info "获取网络信息..."
    
    LOCAL_IP=$(ipconfig getifaddr en0 2>/dev/null)
    if [ -z "$LOCAL_IP" ]; then
        LOCAL_IP=$(ipconfig getifaddr en1 2>/dev/null)
    fi
    
    PUBLIC_IP=$(curl -s https://api.ipify.org 2>/dev/null)
    
    echo "   🏠 本机IP: ${LOCAL_IP:-"无法获取"}"
    echo "   🌐 公网IP: ${PUBLIC_IP:-"无法获取"}"
}

# 检查服务状态
check_services() {
    log_info "检查服务状态..."
    
    if nc -z 127.0.0.1 9000 2>/dev/null; then
        log_success "C服务器 (端口 9000) 正在运行"
    else
        log_warning "C服务器 (端口 9000) 未运行"
    fi
    
    if nc -z 127.0.0.1 8080 2>/dev/null; then
        log_success "Web服务器 (端口 8080) 正在运行"
    else
        log_warning "Web服务器 (端口 8080) 未运行"
    fi
}

# 检查外网访问工具
check_external_tools() {
    log_info "检查外网访问工具..."
    
    # 检查 Cloudflare
    if [ -f "gui_examples/cloudflared-darwin-amd64" ]; then
        log_success "Cloudflare 隧道客户端已安装"
    else
        log_warning "Cloudflare 隧道客户端未安装"
    fi
    
    # 检查 Ngrok
    if command -v ngrok &> /dev/null; then
        log_success "Ngrok 已安装"
    else
        log_warning "Ngrok 未安装 (可选)"
    fi
}

# 测试本地访问
test_local_access() {
    log_info "测试本地访问..."
    
    if curl -s http://localhost:8080 >/dev/null 2>&1; then
        log_success "本地Web访问正常: http://localhost:8080"
    else
        log_warning "本地Web访问失败"
    fi
}

# 显示访问方式
show_access_methods() {
    echo ""
    echo "=================================================="
    echo -e "${BLUE}🌐 外网访问方式${NC}"
    echo "=================================================="
    echo ""
    echo "1. 🌉 Cloudflare 隧道（推荐）"
    echo "   ./start_external_macos.sh"
    echo ""
    echo "2. 🔗 Ngrok 隧道"
    echo "   ngrok http 8080"
    echo ""
    echo "3. 🏠 路由器端口转发"
    echo "   配置路由器转发 8080 端口"
    echo "   访问: http://$PUBLIC_IP:8080"
    echo ""
    echo "4. 🦆 DuckDNS 动态域名"
    echo "   配置动态域名解析"
    echo ""
    echo "📖 详细配置请参考: 外网访问配置指南.md"
}

# 主函数
main() {
    get_network_info
    echo ""
    check_services
    echo ""
    check_external_tools
    echo ""
    test_local_access
    show_access_methods
    
    echo ""
    echo "=================================================="
    echo -e "${GREEN}🎉 测试完成${NC}"
    echo "=================================================="
}

# 运行测试
main
