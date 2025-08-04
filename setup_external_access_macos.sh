#!/bin/bash

# macOS 外网访问配置脚本

echo "🌐 配置 macOS 网盘外网访问"
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

# 获取本机IP地址
get_local_ip() {
    # 尝试多种方法获取本机IP
    LOCAL_IP=$(ipconfig getifaddr en0 2>/dev/null)
    if [ -z "$LOCAL_IP" ]; then
        LOCAL_IP=$(ipconfig getifaddr en1 2>/dev/null)
    fi
    if [ -z "$LOCAL_IP" ]; then
        LOCAL_IP=$(ifconfig | grep "inet " | grep -v 127.0.0.1 | awk '{print $2}' | head -1)
    fi
    echo "$LOCAL_IP"
}

# 获取公网IP地址
get_public_ip() {
    PUBLIC_IP=$(curl -s https://api.ipify.org 2>/dev/null)
    if [ -z "$PUBLIC_IP" ]; then
        PUBLIC_IP=$(curl -s https://icanhazip.com 2>/dev/null)
    fi
    echo "$PUBLIC_IP"
}

# 检查端口是否开放
check_port_open() {
    local port=$1
    if nc -z localhost $port 2>/dev/null; then
        return 0
    else
        return 1
    fi
}

# 主菜单
show_menu() {
    echo ""
    echo "请选择外网访问方式："
    echo "1. 🏠 局域网访问（路由器端口转发）"
    echo "2. 🌉 Cloudflare 隧道（推荐）"
    echo "3. 🔗 Ngrok 隧道"
    echo "4. 🦆 DuckDNS 动态域名"
    echo "5. 📊 查看当前网络状态"
    echo "6. 🚪 退出"
    echo ""
    read -p "请输入选项 (1-6): " choice
}

# 局域网访问配置
setup_lan_access() {
    log_info "配置局域网访问..."
    
    LOCAL_IP=$(get_local_ip)
    if [ -z "$LOCAL_IP" ]; then
        log_error "无法获取本机IP地址"
        return 1
    fi
    
    log_success "本机IP地址: $LOCAL_IP"
    
    echo ""
    echo "📋 局域网访问配置："
    echo "   🔗 Web界面: http://$LOCAL_IP:8080"
    echo "   📡 C服务器: $LOCAL_IP:9000"
    echo ""
    echo "📝 路由器端口转发配置（如需外网访问）："
    echo "   外部端口 8080 -> 内部 $LOCAL_IP:8080 (Web界面)"
    echo "   外部端口 9000 -> 内部 $LOCAL_IP:9000 (C服务器)"
    echo ""
    log_warning "注意：需要在路由器管理界面配置端口转发才能从外网访问"
}

# Cloudflare 隧道配置
setup_cloudflare_tunnel() {
    log_info "配置 Cloudflare 隧道..."
    
    # 检查是否已有 cloudflared
    if [ ! -f "gui_examples/cloudflared-darwin-amd64" ]; then
        log_info "下载 Cloudflare 隧道客户端..."
        cd gui_examples
        curl -L https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-darwin-amd64 -o cloudflared-darwin-amd64
        chmod +x cloudflared-darwin-amd64
        cd ..
        log_success "Cloudflare 客户端下载完成"
    fi
    
    # 检查服务是否运行
    if ! check_port_open 8080; then
        log_warning "Web服务器未运行，请先启动服务："
        echo "   ./start_all_macos.sh"
        return 1
    fi
    
    log_info "启动 Cloudflare 隧道..."
    echo ""
    echo "🌐 Cloudflare 隧道将为您提供一个临时的外网访问地址"
    echo "⚠️  按 Ctrl+C 可以停止隧道"
    echo ""
    
    cd gui_examples
    ./cloudflared-darwin-amd64 tunnel --url http://localhost:8080
}

# Ngrok 隧道配置
setup_ngrok_tunnel() {
    log_info "配置 Ngrok 隧道..."
    
    # 检查是否安装了 ngrok
    if ! command -v ngrok &> /dev/null; then
        log_warning "Ngrok 未安装，正在安装..."
        if command -v brew &> /dev/null; then
            brew install ngrok/ngrok/ngrok
        else
            log_error "请先安装 Homebrew 或手动安装 Ngrok"
            echo "   安装 Homebrew: /bin/bash -c \"\$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\""
            echo "   安装 Ngrok: brew install ngrok/ngrok/ngrok"
            return 1
        fi
    fi
    
    # 检查服务是否运行
    if ! check_port_open 8080; then
        log_warning "Web服务器未运行，请先启动服务："
        echo "   ./start_all_macos.sh"
        return 1
    fi
    
    log_info "启动 Ngrok 隧道..."
    echo ""
    echo "🌐 Ngrok 隧道将为您提供一个临时的外网访问地址"
    echo "⚠️  按 Ctrl+C 可以停止隧道"
    echo ""
    
    ngrok http 8080
}

# DuckDNS 配置
setup_duckdns() {
    log_info "配置 DuckDNS 动态域名..."
    
    PUBLIC_IP=$(get_public_ip)
    if [ -z "$PUBLIC_IP" ]; then
        log_error "无法获取公网IP地址"
        return 1
    fi
    
    log_success "当前公网IP: $PUBLIC_IP"
    
    echo ""
    echo "📋 DuckDNS 配置信息："
    echo "   🌐 域名: tatapan.duckdns.org"
    echo "   🔑 Token: d31c8e89-fa0b-4339-8cc4-738993cf2159"
    echo "   📍 IP地址: $PUBLIC_IP"
    echo ""
    
    # 更新 DuckDNS
    if [ -f "gui_examples/update_duckdns.py" ]; then
        log_info "更新 DuckDNS IP地址..."
        cd gui_examples
        python3 update_duckdns.py
        cd ..
    fi
    
    echo "🔗 外网访问地址: http://tatapan.duckdns.org:8080"
    echo ""
    log_warning "注意：需要在路由器中配置端口转发 8080 -> 本机:8080"
}

# 查看网络状态
show_network_status() {
    log_info "查看当前网络状态..."
    
    LOCAL_IP=$(get_local_ip)
    PUBLIC_IP=$(get_public_ip)
    
    echo ""
    echo "📊 网络状态信息："
    echo "   🏠 本机IP: ${LOCAL_IP:-"无法获取"}"
    echo "   🌐 公网IP: ${PUBLIC_IP:-"无法获取"}"
    echo ""
    
    echo "📡 服务状态："
    if check_port_open 9000; then
        echo "   ✅ C服务器 (端口 9000): 运行中"
    else
        echo "   ❌ C服务器 (端口 9000): 未运行"
    fi
    
    if check_port_open 8080; then
        echo "   ✅ Web服务器 (端口 8080): 运行中"
    else
        echo "   ❌ Web服务器 (端口 8080): 未运行"
    fi
    
    echo ""
    echo "🔗 访问地址："
    echo "   本地: http://localhost:8080"
    if [ ! -z "$LOCAL_IP" ]; then
        echo "   局域网: http://$LOCAL_IP:8080"
    fi
}

# 主程序
main() {
    while true; do
        show_menu
        case $choice in
            1)
                setup_lan_access
                ;;
            2)
                setup_cloudflare_tunnel
                ;;
            3)
                setup_ngrok_tunnel
                ;;
            4)
                setup_duckdns
                ;;
            5)
                show_network_status
                ;;
            6)
                log_info "退出配置"
                exit 0
                ;;
            *)
                log_error "无效选项，请重新选择"
                ;;
        esac
        
        echo ""
        read -p "按回车键继续..." -r
    done
}

# 运行主程序
main
