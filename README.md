# NetDisk - 网盘系统

基于 C 语言开发的轻量级网盘系统，支持 Web 界面和命令行客户端，可在局域网或通过内网穿透实现外网访问。

## 系统架构

```
┌─────────────────┐     HTTP     ┌─────────────────┐     TCP      ┌─────────────────┐
│   Web Browser   │ ◄──────────► │  Flask Server   │ ◄──────────► │   C Server      │
│   (Frontend)    │    :8080     │  (Python API)   │    :9000     │  (Core Engine)  │
└─────────────────┘              └─────────────────┘              └─────────────────┘
                                                                          │
┌─────────────────┐                                               ┌───────▼───────┐
│   CLI Client    │ ◄───────────────────────────────────────---──►│   SQLite DB   │
│   (C Language)  │                  TCP :9000                    │  + File Store │
└─────────────────┘                                               └───────────────┘
```

| 组件 | 端口 | 技术栈 | 功能 |
|------|------|--------|------|
| C Server | 9000 | C + SQLite + OpenSSL + Pthread | 核心文件服务、用户认证、多线程处理 |
| Web Server | 8080 | Python Flask + CORS | RESTful API、静态文件服务 |
| Web Frontend | - | HTML5 + CSS3 + JS ES6+ | 响应式界面、拖拽上传、国际化 |
| CLI Client | - | C + Readline | 智能补全、交互式操作 |
| eBPF Debugger | 8090 | Python + BCC + WebSocket | 网络监控、系统调用追踪、性能分析 |

## 快速开始

### 依赖安装

**Ubuntu/Debian:**
```bash
sudo apt install build-essential libsqlite3-dev libssl-dev libreadline-dev python3 python3-pip
pip3 install flask flask-cors requests
```

**macOS:**
```bash
xcode-select --install
brew install sqlite3 openssl readline
pip3 install flask flask-cors requests
```

### 启动服务

```bash
git clone <repository-url>
cd linux-netdisk
./start.sh
```

启动后访问 http://localhost:8080

### 停止服务

```bash
./stop.sh
```

## 功能特性

### 用户系统
- 注册/登录，SHA-256 密码加密
- 多用户隔离存储

### 文件管理
- 上传/下载（支持进度显示）
- 目录创建/切换/删除
- 文件重命名/删除
- 目录树可视化

### Web 界面
- 响应式设计（PC/移动端）
- 拖拽上传、批量操作
- 中英文切换

### 命令行客户端
- 智能补全（命令/文件名/路径）
- 类 Shell 交互体验

## 命令行客户端使用

```bash
cd client && ./client
```

| 命令 | 说明 | 示例 |
|------|------|------|
| `register <user> <pass>` | 注册 | `register alice 123456` |
| `login <user> <pass>` | 登录 | `login alice 123456` |
| `logout` | 登出 | - |
| `list` | 列出文件 | - |
| `upload <local> <remote>` | 上传 | `upload ~/a.txt docs` |
| `download <file>` | 下载 | `download a.txt` |
| `mkdir <dir>` | 创建目录 | `mkdir photos` |
| `cd <dir>` | 切换目录 | `cd photos` |
| `pwd` | 当前路径 | - |
| `tree` | 目录树 | - |
| `delete <name>` | 删除 | `delete a.txt` |
| `touch <file>` | 创建空文件 | `touch new.txt` |

## API 接口

### 认证
```
POST /api/login     { "username": "...", "password": "..." }
POST /api/register  { "username": "...", "password": "..." }
POST /api/logout
```

### 文件操作
```
GET  /api/files?path=/docs           获取文件列表
POST /api/upload                      上传文件 (multipart/form-data)
GET  /api/download?path=/docs/a.txt  下载文件
POST /api/files/mkdir                 创建目录 { "path": "/", "name": "docs" }
POST /api/files/delete                删除 { "path": "/docs/a.txt" }
POST /api/files/rename                重命名 { "path": "...", "newName": "..." }
```

## eBPF 调试工具

实时监控网盘服务的网络流量、系统调用和性能指标。

### 安装依赖

```bash
sudo apt install bpfcc-tools linux-headers-$(uname -r) python3-bpfcc
pip3 install flask flask-socketio eventlet psutil
```

### 启动

```bash
sudo ./ebpf_debugger/start_debugger.sh
```

访问 http://localhost:8090

### 功能

| 模块 | 监控内容 |
|------|----------|
| Network | TCP 连接/关闭、发送/接收字节、延迟、吞吐量 |
| Syscall | read/write/open/close/accept/sendto/recvfrom 等系统调用 |
| Performance | 上下文切换、CPU 采样 |
| Uprobe | handle_client、send_directory_tree、sha256_string 等用户态函数 |

## 外网访问

使用 Ngrok 实现内网穿透：

```bash
# 安装
brew install ngrok/ngrok/ngrok  # macOS
# 或 Linux: snap install ngrok

# 配置
ngrok config add-authtoken <your-token>

# 启动
./start.sh
ngrok http 8080
```

## 项目结构

```
linux-netdisk/
├── server/main.c           # C 服务器 (945行)
├── client/main.c           # C 客户端 (1246行)
├── gui_examples/
│   ├── app.py              # Flask 主应用
│   ├── api/                # API 模块
│   ├── js/                 # 前端 JS 模块
│   ├── css/                # 样式文件
│   ├── login.html          # 登录页
│   └── dashboard.html      # 主界面
├── ebpf_debugger/
│   ├── app.py              # 调试器主应用
│   ├── bpf_programs/       # eBPF 程序
│   ├── collectors/         # 数据收集器
│   ├── templates/          # Web 模板
│   └── static/             # 静态资源
├── netdisk_data/           # 用户文件存储
├── Makefile                # 编译配置
├── start.sh                # 启动脚本
└── stop.sh                 # 停止脚本
```

## 编译

```bash
make              # 编译全部
make DEBUG=1      # 调试模式
make clean        # 清理
```

## 数据库

```sql
-- 用户表
CREATE TABLE users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT UNIQUE NOT NULL,
    password_hash TEXT NOT NULL,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);
```

文件按用户隔离存储在 `netdisk_data/<username>/` 目录下。

## 故障排除

| 问题 | 解决方案 |
|------|----------|
| 端口被占用 | `./stop.sh` 或 `kill -9 $(lsof -t -i:9000)` |
| 编译失败 | 检查依赖：`pkg-config --cflags sqlite3 openssl` |
| Web 无法访问 | 检查 Flask：`curl http://localhost:8080/api/health` |
| eBPF 不工作 | 需要 root 权限：`sudo ./start_debugger.sh` |

## 安全建议

- 使用强密码（8位以上，含字母数字特殊字符）
- 外网访问用完及时关闭
- 避免在公共网络使用
- 定期备份重要文件

## License

MIT License
