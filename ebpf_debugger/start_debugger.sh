#!/bin/bash

# eBPF Debugger Startup Script
# 用于启动网盘项目的eBPF调试工具

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}"
echo "=============================================="
echo "  eBPF Debugger for NetDisk"
echo "=============================================="
echo -e "${NC}"

# 检查是否以root运行
check_root() {
    if [ "$EUID" -ne 0 ]; then
        echo -e "${YELLOW}[Warning] Not running as root. eBPF features will be limited.${NC}"
        echo -e "${YELLOW}[Tip] Run with: sudo ./start_debugger.sh${NC}"
        echo ""
    else
        echo -e "${GREEN}[OK] Running as root${NC}"
    fi
}

# 检查依赖
check_dependencies() {
    echo -e "${BLUE}[Info] Checking dependencies...${NC}"

    # Python3
    if ! command -v python3 &> /dev/null; then
        echo -e "${RED}[Error] Python3 not found. Please install python3.${NC}"
        exit 1
    fi
    echo -e "${GREEN}[OK] Python3 found${NC}"

    # pip packages
    MISSING_PKGS=""

    if ! python3 -c "import flask" 2>/dev/null; then
        MISSING_PKGS="$MISSING_PKGS flask"
    fi

    if ! python3 -c "import flask_socketio" 2>/dev/null; then
        MISSING_PKGS="$MISSING_PKGS flask-socketio"
    fi

    if ! python3 -c "import eventlet" 2>/dev/null; then
        MISSING_PKGS="$MISSING_PKGS eventlet"
    fi

    if ! python3 -c "import psutil" 2>/dev/null; then
        MISSING_PKGS="$MISSING_PKGS psutil"
    fi

    if [ -n "$MISSING_PKGS" ]; then
        echo -e "${YELLOW}[Warning] Missing packages:$MISSING_PKGS${NC}"
        echo -e "${YELLOW}[Info] Installing missing packages...${NC}"
        pip3 install --break-system-packages $MISSING_PKGS
    fi

    # BCC (optional but recommended)
    if ! python3 -c "from bcc import BPF" 2>/dev/null; then
        echo -e "${YELLOW}[Warning] BCC not installed. eBPF monitoring will not work.${NC}"
        echo -e "${YELLOW}[Tip] Install with: sudo apt-get install bpfcc-tools python3-bpfcc${NC}"
        BCC_AVAILABLE=0
    else
        echo -e "${GREEN}[OK] BCC found${NC}"
        BCC_AVAILABLE=1
    fi

    echo ""
}

# 安装BCC
install_bcc() {
    echo -e "${BLUE}[Info] Installing BCC...${NC}"

    # 更新包列表
    sudo apt-get update

    # 安装BCC和依赖
    sudo apt-get install -y \
        bpfcc-tools \
        linux-headers-$(uname -r) \
        python3-bpfcc \
        libbpf-dev

    echo -e "${GREEN}[OK] BCC installed${NC}"
}

# 启动调试器
start_debugger() {
    echo -e "${BLUE}[Info] Starting eBPF Debugger...${NC}"
    echo -e "${BLUE}[Info] Dashboard will be available at http://localhost:8090${NC}"
    echo ""

    # 设置Python路径
    export PYTHONPATH="$SCRIPT_DIR:$PYTHONPATH"

    # 启动应用
    if [ "$EUID" -eq 0 ]; then
        python3 app.py
    else
        echo -e "${YELLOW}[Warning] Running without root - eBPF features disabled${NC}"
        python3 app.py
    fi
}

# 显示帮助
show_help() {
    echo "Usage: $0 [options]"
    echo ""
    echo "Options:"
    echo "  --install-deps    Install all dependencies including BCC"
    echo "  --check           Check dependencies only"
    echo "  --help            Show this help message"
    echo ""
    echo "Examples:"
    echo "  sudo ./start_debugger.sh              # Start with full eBPF support"
    echo "  ./start_debugger.sh                   # Start without eBPF (demo mode)"
    echo "  sudo ./start_debugger.sh --install-deps  # Install deps and start"
}

# 主函数
main() {
    case "${1:-}" in
        --install-deps)
            check_root
            check_dependencies
            if [ "$BCC_AVAILABLE" -eq 0 ]; then
                install_bcc
            fi
            start_debugger
            ;;
        --check)
            check_root
            check_dependencies
            echo -e "${GREEN}[Done] Dependency check complete${NC}"
            ;;
        --help|-h)
            show_help
            ;;
        *)
            check_root
            check_dependencies
            start_debugger
            ;;
    esac
}

main "$@"
