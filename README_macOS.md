# macOS 网盘项目部署指南

## 🍎 系统要求

- macOS 10.15 或更高版本
- Xcode Command Line Tools
- Homebrew 包管理器

## 📦 依赖安装

### 1. 安装 Homebrew（如果未安装）
```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

### 2. 安装必要的依赖
```bash
# 安装编译工具
xcode-select --install

# 安装项目依赖
brew install sqlite3 openssl readline python3

# 安装Python依赖
pip3 install flask requests
```

## 🚀 快速启动

### 方法一：一键启动（推荐）

**完整版启动**（带详细日志和监控）
```bash
./start_all_macos.sh
```

**快速启动**（后台运行）
```bash
./quick_start.sh
```

**停止所有服务**
```bash
./stop_all_macos.sh
```

### 方法二：分别启动

1. **启动服务器**
```bash
./start_server_macos.sh
```

2. **启动客户端**（在新终端窗口中）
```bash
./start_client_macos.sh
```

3. **启动Web界面**（可选）
```bash
cd gui_examples
./start_server_macos.sh
```

### 方法二：手动启动

1. **编译项目**
```bash
make clean
make
```

2. **启动服务器**
```bash
cd server
./server
```

3. **启动客户端**（在新终端窗口中）
```bash
cd client
./client
```

## 🌐 Web界面访问

### 本地和局域网访问
启动Web服务器后，可以通过以下地址访问：
- 本地访问：http://localhost:8080
- 局域网访问：http://[你的IP]:8080

### 外网访问
支持多种外网访问方式：

**一键外网访问（推荐）**
```bash
./start_external_macos.sh
```

**配置向导**
```bash
./setup_external_access_macos.sh
```

**手动启动隧道**
```bash
cd gui_examples
./start_external_access.sh
```

详细配置请参考：[外网访问配置指南.md](外网访问配置指南.md)

## 📁 项目结构

```
linux-netdisk/
├── server/                 # C语言服务器
│   ├── main.c
│   └── server             # 编译后的可执行文件
├── client/                 # C语言客户端
│   ├── main.c
│   └── client             # 编译后的可执行文件
├── gui_examples/           # Web界面
│   ├── app.py             # Flask Web服务器
│   ├── api/               # API接口
│   └── start_server_macos.sh
├── netdisk_data/          # 用户数据目录
├── netdisk.db             # SQLite数据库
├── start_all_macos.sh          # 一键启动脚本（完整版）
├── start_external_macos.sh     # 一键外网访问脚本
├── setup_external_access_macos.sh # 外网访问配置向导
├── quick_start.sh              # 快速启动脚本
├── stop_all_macos.sh           # 停止服务脚本
├── start_server_macos.sh       # 服务器启动脚本
├── start_client_macos.sh       # 客户端启动脚本
├── 外网访问配置指南.md          # 外网访问详细说明
└── Makefile                    # 编译配置
```

## 🔧 配置说明

### 服务器配置
- 端口：9000（C服务器）
- 数据库：SQLite（netdisk.db）
- 数据目录：netdisk_data/

### Web服务器配置
- 端口：8080
- 地址：0.0.0.0（允许局域网访问）

## 🛠️ 故障排除

### 编译错误
1. **找不到头文件**
```bash
# 确保安装了必要的依赖
brew install sqlite3 openssl readline
```

2. **链接错误**
```bash
# 重新编译
make clean
make
```

### 运行时错误
1. **端口被占用**
```bash
# 查找占用端口的进程
lsof -i :9000
# 终止进程
kill -9 <PID>
```

2. **权限错误**
```bash
# 确保脚本有执行权限
chmod +x start_server_macos.sh
chmod +x start_client_macos.sh
```

## 🌟 功能特性

- ✅ 用户注册和登录
- ✅ 文件上传和下载
- ✅ 目录管理（创建、切换、列表）
- ✅ 文件删除
- ✅ 进度条显示
- ✅ 命令行自动补全
- ✅ Web界面管理
- ✅ 跨平台支持（macOS/Linux）

## 📞 技术支持

如果遇到问题，请检查：
1. 所有依赖是否正确安装
2. 端口是否被占用
3. 文件权限是否正确
4. 防火墙设置是否允许相应端口

## 🔄 从Ubuntu迁移

如果您之前在Ubuntu上部署过此项目，主要变化：
1. 服务器IP从 `192.168.50.153` 改为 `127.0.0.1`
2. 使用macOS专用的启动脚本
3. 依赖管理使用Homebrew而不是apt
4. 字节序转换使用macOS专用的API
