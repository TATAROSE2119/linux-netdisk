# 🌐 网盘系统

一个基于 C 语言开发的完整网盘系统，支持文件上传、下载、管理，提供命令行和 Web 两种客户端界面。

## ✨ 功能特性

### 🔐 用户系统
- 用户注册、登录、密码 SHA-256 加密存储
- 多用户隔离，每个用户拥有独立的文件空间
- 会话管理和权限控制

### 📁 文件管理
- **上传**：支持单文件和批量上传，显示进度条
- **下载**：支持文件下载，断点续传
- **删除**：支持文件和目录删除（包括递归删除）
- **目录管理**：创建、切换、删除目录
- **重命名**：文件和目录重命名功能
- **目录树**：可视化目录结构展示

### 🌐 Web界面
- **现代化设计**：响应式布局，支持移动端
- **拖拽上传**：支持文件拖拽上传
- **批量操作**：支持多文件选择和批量下载
- **实时进度**：文件传输进度条和速度显示
- **国际化**：支持中英文一键切换
- **移动端优化**：触摸友好的界面设计

### 💻 命令行客户端
- **智能补全**：命令和文件名自动补全
- **交互式界面**：类似 shell 的操作体验
- **进度显示**：文件传输进度条
- **目录导航**：支持相对路径和绝对路径
- **历史记录**：命令历史记录功能

### 🔗 网络功能
- **外网访问**：支持通过 Ngrok 实现外网访问
- **局域网访问**：支持局域网内多设备访问
- **API接口**：RESTful API 设计
- **CORS支持**：跨域资源共享

## 🏗️ 系统架构

```
网盘系统
├── server/                 # C语言服务器 (端口: 9000)
│   └── main.c             # 服务器主程序，处理文件操作和用户认证
├── client/                 # C语言命令行客户端
│   └── main.c             # 客户端主程序，支持智能补全
├── gui_examples/           # Web服务器和客户端 (端口: 8080)
│   ├── app.py             # Flask Web服务器
│   ├── api/               # RESTful API接口
│   │   ├── auth.py        # 用户认证API
│   │   ├── files.py       # 文件操作API
│   │   └── upload.py      # 文件上传API
│   ├── css/               # 样式文件
│   │   ├── dashboard.css  # 主界面样式（响应式）
│   │   └── login.css      # 登录页面样式
│   ├── js/                # JavaScript模块
│   │   ├── i18n-manager.js    # 国际化管理器
│   │   ├── file-manager.js    # 文件管理器
│   │   ├── upload-manager.js  # 上传管理器
│   │   ├── download-manager.js # 下载管理器
│   │   ├── auth-manager.js    # 认证管理器
│   │   └── ui-utils.js        # UI工具类
│   ├── login.html         # 登录页面（支持国际化）
│   ├── dashboard.html     # 主界面（响应式设计）
│   ├── mobile-test.html   # 移动端测试页面
│   └── i18n-demo.html     # 国际化演示页面
├── netdisk_data/          # 用户文件存储目录
├── Makefile              # 编译配置
├── start.sh              # 一键启动脚本
├── stop.sh               # 停止服务脚本
└── README.md             # 项目文档
```

### 技术栈

**后端**：
- **C语言**：高性能的文件服务器
- **SQLite3**：轻量级数据库，存储用户信息
- **OpenSSL**：密码加密和安全传输
- **Socket编程**：TCP网络通信

**前端**：
- **HTML5/CSS3**：现代化Web界面
- **JavaScript ES6+**：模块化前端架构
- **响应式设计**：支持桌面和移动设备
- **国际化**：中英文切换支持

**Web服务器**：
- **Python Flask**：轻量级Web框架
- **RESTful API**：标准化接口设计
- **CORS支持**：跨域资源共享

## 🚀 快速开始

### 系统要求

- **macOS** 10.15 或更高版本（主要测试平台）
- **Linux** Ubuntu 18.04+ / CentOS 7+（兼容）
- **Xcode Command Line Tools**（macOS）或 **GCC**（Linux）
- **Python 3.7+**
- **SQLite3**
- **OpenSSL**

### 安装依赖

**macOS**：
```bash
# 安装 Xcode Command Line Tools
xcode-select --install

# 安装 Homebrew（如果未安装）
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# 安装依赖
brew install sqlite3 openssl
pip3 install flask flask-cors requests
```

**Linux (Ubuntu/Debian)**：
```bash
# 安装编译工具和依赖
sudo apt update
sudo apt install build-essential sqlite3 libsqlite3-dev libssl-dev python3 python3-pip

# 安装 Python 依赖
pip3 install flask flask-cors requests
```

**Linux (CentOS/RHEL)**：
```bash
# 安装编译工具和依赖
sudo yum groupinstall "Development Tools"
sudo yum install sqlite-devel openssl-devel python3 python3-pip

# 安装 Python 依赖
pip3 install flask flask-cors requests
```

### 一键启动

```bash
# 克隆项目
git clone <repository-url>
cd linux-netdisk

# 启动服务
./start.sh
```

启动成功后，您将看到：
```
🎉 网盘服务启动完成！
================================================

📱 访问地址:
   🔗 本地访问: http://localhost:8080
   🏠 局域网访问: http://192.168.1.100:8080

🌐 外网访问 (使用 Ngrok):
   1. 安装 Ngrok: brew install ngrok/ngrok/ngrok
   2. 注册账号: https://ngrok.com/
   3. 配置令牌: ngrok config add-authtoken <your-token>
   4. 启动隧道: ngrok http 8080
   5. 使用 Ngrok 提供的 URL 访问

💡 功能特色:
   ✅ 响应式Web界面，支持移动端
   ✅ 中英文一键切换
   ✅ 智能文件名补全
   ✅ 拖拽上传，批量操作
   ✅ 实时进度显示
```

### 停止服务

```bash
# 停止所有服务
./stop.sh

# 或者按 Ctrl+C 停止
```
## 🌐 外网访问和移动端使用

### 🔗 外网访问配置

#### 使用 Ngrok 实现外网访问

1. **安装 Ngrok**
```bash
# macOS
brew install ngrok/ngrok/ngrok

# Linux
curl -s https://ngrok-agent.s3.amazonaws.com/ngrok.asc | sudo tee /etc/apt/trusted.gpg.d/ngrok.asc >/dev/null
echo "deb https://ngrok-agent.s3.amazonaws.com buster main" | sudo tee /etc/apt/sources.list.d/ngrok.list
sudo apt update && sudo apt install ngrok
```

2. **注册和配置**
```bash
# 访问 https://ngrok.com/ 注册账号
# 获取认证令牌后配置
ngrok config add-authtoken <your-auth-token>
```

3. **启动外网访问**
```bash
# 启动网盘服务
./start.sh

# 在新终端启动 Ngrok
ngrok http 8080
```

4. **访问外网地址**
```
Ngrok 会显示类似信息：
┌─────────────────────────────────────────────────────┐
│ Forwarding https://abc123.ngrok.io -> localhost:8080 │
└─────────────────────────────────────────────────────┘

使用 https://abc123.ngrok.io 从任何地方访问您的网盘
```

#### Ngrok 版本对比

| 功能 | 免费版 | 付费版 |
|------|--------|--------|
| 隧道数量 | 1个 | 多个 |
| 域名 | 随机生成 | 自定义固定域名 |
| 带宽限制 | 有限制 | 更高带宽 |
| 会话时长 | 8小时 | 无限制 |
| 价格 | 免费 | $8/月起 |

### 📱 移动端访问

#### 局域网访问
```bash
# 1. 确保设备在同一WiFi网络
# 2. 启动网盘服务
./start.sh

# 3. 查看本机IP地址
ifconfig | grep "inet " | grep -v 127.0.0.1

# 4. 在移动设备浏览器访问
# http://192.168.1.100:8080 (替换为实际IP)
```

#### 移动端功能特性
- **📱 响应式设计**：自动适配手机和平板屏幕
- **👆 触摸优化**：
  - 按钮大小符合触摸标准（44px最小）
  - 复选框放大便于选择
  - 触摸反馈效果
- **📋 手势操作**：
  - 拖拽上传文件
  - 左右滑动查看文件列表
  - 长按选择文件
- **🌐 语言切换**：支持中英文一键切换
- **📊 进度显示**：文件上传下载进度条

#### 移动端使用技巧
1. **文件上传**：
   - 点击上传按钮选择文件
   - 支持相机拍照直接上传
   - 支持从相册选择多张图片
2. **文件管理**：
   - 点击目录名进入文件夹
   - 使用"返回"按钮导航
   - 长按文件名查看详细信息
3. **批量操作**：
   - 勾选多个文件
   - 使用批量下载功能
   - 批量删除不需要的文件

### 🔒 安全建议

#### 本地使用安全
- ✅ 默认只监听本地地址（127.0.0.1）
- ✅ 用户密码使用 SHA-256 加密存储
- ✅ 每个用户拥有独立的文件空间
- ✅ 建议使用强密码（8位以上，包含字母数字特殊字符）

#### 外网访问安全
- ⚠️ **外网访问存在安全风险，请谨慎使用**
- ✅ 使用强密码保护账户
- ✅ 使用完毕及时关闭外网访问
- ✅ 避免在公共网络环境下使用
- ✅ 定期更换 Ngrok URL
- ✅ 不要长期开放外网访问
- ✅ 考虑使用 HTTPS 加密传输
- ✅ 定期备份重要文件

## 💻 使用指南

### 🌐 Web 客户端使用

#### 1. 访问和登录
```
1. 打开浏览器访问: http://localhost:8080
2. 首次使用点击"注册新账户"
3. 输入用户名和密码完成注册
4. 使用注册的账户登录系统
```

#### 2. 界面功能
- **🌐 语言切换**：点击右上角的语言按钮切换中英文
- **📍 路径导航**：显示当前目录位置，支持快速返回
- **🛠️ 工具栏**：包含所有文件操作功能
- **📁 文件列表**：显示文件和目录，支持排序和筛选
- **📊 状态栏**：显示操作状态和进度

#### 3. 文件操作
- **📤 上传文件**：
  - 方法1：拖拽文件到上传区域
  - 方法2：点击"上传文件"按钮选择文件
  - 支持批量上传和进度显示
- **📥 下载文件**：
  - 单文件：点击文件名或"选择"按钮后点击"下载"
  - 批量下载：选择多个文件后点击"批量下载"
- **🗑️ 删除文件**：选择文件后点击"删除"按钮
- **✏️ 重命名**：选择文件后点击"重命名"按钮
- **📁 目录管理**：
  - 创建：点击"新建文件夹"按钮
  - 进入：双击目录名或点击"进入"按钮
  - 返回：点击"返回上级"按钮

#### 4. 移动端使用
- **响应式设计**：自动适配手机和平板屏幕
- **触摸优化**：按钮大小适合触摸操作
- **滑动查看**：文件列表支持左右滑动查看更多信息
- **手势操作**：支持拖拽上传和触摸选择

### 💻 命令行客户端使用

#### 1. 启动客户端
```bash
cd client
./client
```

#### 2. 用户管理
```bash
# 注册新用户
Netdisk> register alice password123
✅ 注册成功

# 登录系统
Netdisk> login alice password123
✅ 登录成功
Netdisk[alice]/>
```

#### 3. 目录操作
```bash
# 查看当前目录
Netdisk[alice]/> pwd
当前路径: /

# 创建目录
Netdisk[alice]/> mkdir documents
✅ 目录创建成功

# 切换目录
Netdisk[alice]/> cd documents
✅ 切换到目录: /documents

# 返回上级目录
Netdisk[alice]/documents> cd ..
✅ 切换到目录: /

# 查看目录树
Netdisk[alice]/> tree
📁 /
├── 📁 documents/
└── 📁 photos/
```

#### 4. 文件操作
```bash
# 上传文件
Netdisk[alice]/> upload ~/Desktop/test.txt documents
📤 正在上传文件...
✅ 文件上传成功

# 查看文件列表
Netdisk[alice]/> list
📁 当前目录 (/) 共 2 个项目:
名称                    大小        修改时间
documents/              -           2024-01-15 10:30
photos/                 -           2024-01-15 10:25

# 下载文件
Netdisk[alice]/documents> download test.txt
📥 正在下载文件...
✅ 文件下载成功: test.txt

# 删除文件
Netdisk[alice]/documents> delete test.txt
✅ 删除成功: test.txt
```

#### 5. 智能补全功能
- **命令补全**：按Tab键补全命令名
- **文件名补全**：
  - `cd` 命令：只补全目录名
  - `download` 命令：只补全文件名
  - `delete` 命令：补全文件和目录名
  - `upload` 命令：本地文件补全 + 服务器目录补全
- **路径补全**：支持相对路径（`../`, `./`）补全

#### 6. 可用命令列表
| 命令 | 语法 | 功能 | 示例 |
|------|------|------|------|
| `register` | `register <用户名> <密码>` | 注册新用户 | `register alice 123456` |
| `login` | `login <用户名> <密码>` | 登录系统 | `login alice 123456` |
| `logout` | `logout` | 退出登录 | `logout` |
| `upload` | `upload <本地文件> <目标目录>` | 上传文件 | `upload ~/file.txt documents` |
| `download` | `download <文件名>` | 下载文件 | `download file.txt` |
| `list` | `list` | 查看文件列表 | `list` |
| `mkdir` | `mkdir <目录名>` | 创建目录 | `mkdir photos` |
| `cd` | `cd <目录名>` | 切换目录 | `cd photos` |
| `pwd` | `pwd` | 显示当前目录 | `pwd` |
| `tree` | `tree` | 显示目录树 | `tree` |
| `delete` | `delete <文件名>` | 删除文件/目录 | `delete file.txt` |
| `touch` | `touch <文件名>` | 创建空文件 | `touch newfile.txt` |
| `help` | `help` | 显示帮助信息 | `help` |
| `exit` | `exit` | 退出客户端 | `exit` |
## 🔧 开发说明

### 编译和构建

#### 编译项目
```bash
# 编译所有组件
make

# 编译服务器
make server/server

# 编译客户端
make client/client

# 清理编译文件
make clean

# 使用 bear 生成编译数据库（用于IDE支持）
make clean && bear -- make
```

#### 编译选项
```bash
# Debug 模式编译
make DEBUG=1

# 启用详细输出
make VERBOSE=1

# 指定编译器
make CC=clang
```

### 项目架构详解

#### 🖥️ C语言服务器 (server/main.c)
- **端口**：9000
- **协议**：TCP Socket 通信
- **数据库**：SQLite3 存储用户信息
- **加密**：SHA-256 密码加密
- **功能模块**：
  - 用户认证和会话管理
  - 文件上传下载处理
  - 目录操作和权限控制
  - 多线程并发处理

#### 💻 命令行客户端 (client/main.c)
- **连接**：127.0.0.1:9000
- **依赖**：readline 库（命令行编辑）
- **特性**：
  - 智能命令补全
  - 文件名自动补全
  - 进度条显示
  - 历史记录功能
  - 交互式shell界面

#### 🌐 Web服务器 (gui_examples/app.py)
- **端口**：8080
- **框架**：Flask + Flask-CORS
- **API设计**：RESTful 风格
- **功能模块**：
  - 用户认证API (`/api/auth/`)
  - 文件操作API (`/api/files/`)
  - 上传下载API (`/api/upload/`, `/api/download/`)
  - 静态文件服务

#### 🎨 Web前端 (gui_examples/)
- **技术栈**：
  - HTML5 + CSS3 + JavaScript ES6+
  - 模块化架构设计
  - 响应式布局（Bootstrap风格）
- **核心模块**：
  - `i18n-manager.js`：国际化管理
  - `file-manager.js`：文件操作逻辑
  - `upload-manager.js`：上传队列管理
  - `auth-manager.js`：用户认证
  - `ui-utils.js`：UI工具函数

### 🔌 API接口文档

#### 认证接口
```http
POST /api/auth/register
Content-Type: application/json
{
  "username": "alice",
  "password": "password123"
}

POST /api/auth/login
Content-Type: application/json
{
  "username": "alice",
  "password": "password123"
}

POST /api/auth/logout
Authorization: Bearer <token>
```

#### 文件操作接口
```http
GET /api/files/list?path=/documents
Authorization: Bearer <token>

POST /api/files/mkdir
Content-Type: application/json
Authorization: Bearer <token>
{
  "path": "/documents",
  "name": "new_folder"
}

DELETE /api/files/delete
Content-Type: application/json
Authorization: Bearer <token>
{
  "path": "/documents/file.txt"
}
```

#### 上传下载接口
```http
POST /api/upload
Content-Type: multipart/form-data
Authorization: Bearer <token>
file: <binary_data>
path: /documents

GET /api/download?path=/documents/file.txt
Authorization: Bearer <token>
```

### 🗄️ 数据库设计

#### 用户表 (users)
```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT UNIQUE NOT NULL,
    password_hash TEXT NOT NULL,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);
```

#### 文件存储结构
```
netdisk_data/
├── alice/              # 用户目录
│   ├── documents/      # 用户子目录
│   │   └── file.txt   # 用户文件
│   └── photos/
└── bob/
    └── uploads/
```

### 🔧 配置说明

#### 服务器配置
- **端口配置**：修改 `server/main.c` 中的 `PORT` 宏
- **数据库路径**：默认为 `server/netdisk.db`
- **文件存储路径**：默认为 `netdisk_data/`
- **最大连接数**：可在代码中调整线程池大小

#### Web服务器配置
- **端口配置**：修改 `gui_examples/app.py` 中的端口设置
- **CORS配置**：在 `app.py` 中配置允许的域名
- **上传限制**：可配置最大文件大小限制

## 🛡️ 安全说明

### 本地使用
- 默认只监听本地地址，相对安全
- 用户密码使用 SHA-256 加密存储
- 建议使用强密码

### 外网访问安全建议
- ⚠️ **外网访问存在安全风险**，请谨慎使用
- ✅ 使用强密码保护账户
- ✅ 使用完毕及时关闭外网访问
- ✅ 避免在公共网络环境下使用
- ✅ 定期更换 Ngrok URL
- ✅ 不要长期开放外网访问

## 🐛 故障排除

### 常见问题和解决方案

#### 1. 编译问题

**问题**：编译失败，找不到头文件
```bash
# macOS 解决方案
xcode-select --install
brew install sqlite3 openssl readline

# Linux 解决方案
sudo apt install build-essential libsqlite3-dev libssl-dev libreadline-dev
```

**问题**：链接错误，找不到库文件
```bash
# 检查库文件路径
pkg-config --cflags --libs sqlite3 openssl

# 手动指定库路径（如果需要）
export LDFLAGS="-L/usr/local/lib"
export CPPFLAGS="-I/usr/local/include"
```

#### 2. 网络连接问题

**问题**：端口被占用
```bash
# 查看端口占用情况
lsof -i :9000    # C服务器端口
lsof -i :8080    # Web服务器端口

# 强制停止占用进程
./stop.sh
# 或手动杀死进程
kill -9 $(lsof -t -i:9000)
kill -9 $(lsof -t -i:8080)
```

**问题**：无法连接到服务器
```bash
# 检查服务器是否启动
ps aux | grep server
netstat -an | grep :9000

# 检查防火墙设置
sudo ufw status    # Linux
# macOS 在系统偏好设置中检查防火墙
```

#### 3. Web界面问题

**问题**：无法访问Web界面
```bash
# 检查Web服务器状态
curl http://localhost:8080
curl -I http://localhost:8080/api/health

# 检查Python依赖
pip3 list | grep -E "(flask|requests|cors)"

# 重新安装依赖
pip3 install --upgrade flask flask-cors requests
```

**问题**：上传文件失败
- 检查文件大小是否超过限制
- 检查磁盘空间是否充足
- 检查文件权限设置

#### 4. 客户端问题

**问题**：命令行客户端无法启动
```bash
# 检查可执行文件权限
chmod +x client/client

# 检查依赖库
ldd client/client    # Linux
otool -L client/client    # macOS
```

**问题**：自动补全不工作
- 确保安装了 readline 库
- 检查终端是否支持 readline
- 尝试重新编译客户端

#### 5. 数据问题

**问题**：用户数据丢失
```bash
# 检查数据库文件
ls -la server/netdisk.db
sqlite3 server/netdisk.db ".tables"

# 检查用户文件目录
ls -la netdisk_data/
```

**问题**：重置系统数据
```bash
# ⚠️ 警告：此操作会删除所有数据
./stop.sh
rm -rf server/netdisk.db netdisk_data/
./start.sh
```

### 🔍 调试和日志

#### 启用调试模式
```bash
# 编译调试版本
make clean
make DEBUG=1

# 启动时显示详细日志
./start.sh --verbose
```

#### 查看日志
```bash
# macOS 系统日志
log show --predicate 'process == "server"' --last 1h

# Linux 系统日志
journalctl -u netdisk-server -f

# 查看网络连接
netstat -tulpn | grep -E ":8080|:9000"
ss -tulpn | grep -E ":8080|:9000"
```

#### 性能监控
```bash
# 监控进程资源使用
top -p $(pgrep -f server)
htop -p $(pgrep -f server)

# 监控网络流量
iftop -i en0    # 替换为实际网卡名
```

### 📊 性能优化建议

#### 服务器优化
- 调整线程池大小以适应并发需求
- 使用SSD存储提高文件I/O性能
- 配置适当的缓存策略
- 监控内存使用情况

#### 网络优化
- 使用有线连接获得更好的传输速度
- 在局域网环境下使用以获得最佳性能
- 避免在高延迟网络环境下传输大文件

#### 客户端优化
- 大文件传输建议使用命令行客户端
- 批量操作时避免并发过多请求
- 定期清理下载目录

## 📝 更新日志

### v2.0.0 (最新版本)
- 🌐 **国际化支持**：中英文一键切换
- 📱 **移动端优化**：完整的响应式设计和触摸优化
- 🎯 **智能补全**：命令行客户端支持文件名和路径补全
- 🔧 **功能增强**：
  - 文件和目录删除功能
  - 批量文件操作
  - 实时进度显示
  - 拖拽上传支持
- 🎨 **界面改进**：
  - 现代化的Web界面设计
  - 移动端自适应布局
  - 触摸友好的交互体验
- 🛠️ **技术优化**：
  - 模块化的前端架构
  - RESTful API设计
  - 更好的错误处理

### v1.5.0
- ✅ Web界面重构，支持响应式设计
- ✅ 添加文件上传进度显示
- ✅ 改进命令行客户端用户体验
- ✅ 增加目录树显示功能
- ✅ 优化文件传输性能

### v1.0.0
- ✅ 基础的文件上传下载功能
- ✅ 用户注册登录系统
- ✅ Web 和命令行双客户端
- ✅ 目录管理功能
- ✅ 外网访问支持

## 🚀 未来计划

### 短期计划 (v2.1.0)
- [ ] 文件搜索功能
- [ ] 文件分享链接
- [ ] 用户权限管理
- [ ] 文件版本控制
- [ ] 回收站功能

### 长期计划 (v3.0.0)
- [ ] 多用户协作功能
- [ ] 文件同步客户端
- [ ] 移动端原生应用
- [ ] 云存储集成
- [ ] 企业级功能

## 🤝 贡献指南

我们欢迎所有形式的贡献！

### 如何贡献
1. **Fork** 本项目
2. 创建您的功能分支 (`git checkout -b feature/AmazingFeature`)
3. 提交您的更改 (`git commit -m 'Add some AmazingFeature'`)
4. 推送到分支 (`git push origin feature/AmazingFeature`)
5. 打开一个 **Pull Request**

### 贡献类型
- 🐛 **Bug修复**：修复现有功能的问题
- ✨ **新功能**：添加新的功能特性
- 📚 **文档**：改进文档和示例
- 🎨 **界面**：改进用户界面和体验
- ⚡ **性能**：优化性能和效率
- 🔧 **重构**：代码重构和架构改进

### 开发环境设置
```bash
# 1. 克隆项目
git clone <your-fork-url>
cd linux-netdisk

# 2. 安装依赖
./install-deps.sh

# 3. 编译项目
make

# 4. 运行测试
make test

# 5. 启动开发服务器
./start.sh
```

### 代码规范
- **C代码**：遵循 GNU C 编码规范
- **Python代码**：遵循 PEP 8 规范
- **JavaScript代码**：使用 ES6+ 语法，遵循 Airbnb 规范
- **提交信息**：使用清晰的提交信息，遵循 Conventional Commits

## 📄 许可证

本项目采用 MIT 许可证 - 查看 [LICENSE](LICENSE) 文件了解详情。

### MIT License 说明
- ✅ 商业使用
- ✅ 修改
- ✅ 分发
- ✅ 私人使用
- ❌ 责任
- ❌ 保证

## 📞 技术支持

### 获取帮助
如果遇到问题，请按以下顺序寻求帮助：

1. **📖 查看文档**：
   - 阅读本 README 文档
   - 查看故障排除部分
   - 检查 API 文档

2. **🔍 搜索已知问题**：
   - 查看 [Issues](../../issues) 页面
   - 搜索相关关键词
   - 查看已关闭的问题

3. **💬 提交新问题**：
   - 使用问题模板
   - 提供详细的错误信息
   - 包含系统环境信息
   - 提供复现步骤

4. **📧 联系方式**：
   - GitHub Issues（推荐）
   - 邮件支持：support@example.com

### 问题报告模板
```markdown
**问题描述**
简要描述遇到的问题

**复现步骤**
1. 执行 '...'
2. 点击 '....'
3. 看到错误

**期望行为**
描述您期望发生的情况

**实际行为**
描述实际发生的情况

**环境信息**
- 操作系统: [例如 macOS 12.0]
- 浏览器: [例如 Chrome 95.0]
- 项目版本: [例如 v2.0.0]

**附加信息**
添加任何其他相关信息、截图或日志
```

## 🙏 致谢

感谢所有为这个项目做出贡献的开发者和用户！

### 特别感谢
- **开源社区**：提供了优秀的开源库和工具
- **测试用户**：提供了宝贵的反馈和建议
- **贡献者**：提交了代码、文档和问题报告

### 使用的开源项目
- [SQLite](https://sqlite.org/) - 轻量级数据库
- [OpenSSL](https://openssl.org/) - 加密库
- [Flask](https://flask.palletsprojects.com/) - Python Web框架
- [Readline](https://tiswww.case.edu/php/chet/readline/rltop.html) - 命令行编辑库

---

<div align="center">

**🌟 如果这个项目对您有帮助，请给我们一个 Star！**

[![GitHub stars](https://img.shields.io/github/stars/username/linux-netdisk.svg?style=social&label=Star)](https://github.com/username/linux-netdisk)
[![GitHub forks](https://img.shields.io/github/forks/username/linux-netdisk.svg?style=social&label=Fork)](https://github.com/username/linux-netdisk/fork)

</div>
