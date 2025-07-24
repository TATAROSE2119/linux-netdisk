/**
 * 认证管理模块
 * 负责用户登录、注册、登出等认证相关功能
 */

class AuthManager {
    constructor() {
        this.currentUser = null;
        this.isLoggedIn = false;
        this.storageKey = 'netdisk_user';
    }

    // 检查登录状态
    checkLoginStatus() {
        const savedUser = localStorage.getItem(this.storageKey);
        if (savedUser) {
            this.currentUser = savedUser;
            this.isLoggedIn = true;
            return true;
        }
        return false;
    }

    // 保存登录状态
    saveLoginStatus(username) {
        localStorage.setItem(this.storageKey, username);
        this.currentUser = username;
        this.isLoggedIn = true;
    }

    // 清除登录状态
    clearLoginStatus() {
        localStorage.removeItem(this.storageKey);
        this.currentUser = null;
        this.isLoggedIn = false;
    }

    // 登录
    async login(username, password) {
        if (!username || !password) {
            throw new Error('请输入用户名和密码');
        }

        try {
            const response = await fetch(window.apiConfig.loginUrl, {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
                body: JSON.stringify({
                    username: username,
                    password: password
                })
            });

            const data = await response.json();
            
            if (data.success) {
                this.saveLoginStatus(username);
                return { success: true, message: '登录成功' };
            } else {
                return { success: false, message: data.message || '登录失败' };
            }
        } catch (error) {
            console.log('登录请求失败:', error);
            return { success: false, message: '网络连接失败，请检查服务器状态' };
        }
    }

    // 注册
    async register(username, password) {
        if (!username || !password) {
            throw new Error('请输入用户名和密码');
        }

        if (username.trim().length < 3) {
            throw new Error('用户名至少需要3个字符');
        }

        if (password.length < 6) {
            throw new Error('密码至少需要6个字符');
        }

        try {
            const response = await fetch(window.apiConfig.registerUrl, {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
                body: JSON.stringify({
                    username: username.trim(),
                    password: password
                })
            });

            const data = await response.json();
            
            if (data.success) {
                return { success: true, message: '注册成功' };
            } else {
                return { success: false, message: data.message || '注册失败' };
            }
        } catch (error) {
            console.log('注册请求失败:', error);
            return { success: false, message: '网络连接失败，请检查服务器状态' };
        }
    }

    // 登出
    logout() {
        this.clearLoginStatus();
        
        // 清理其他模块的状态
        if (window.fileManager) {
            fileManager.currentUser = '';
            fileManager.currentPath = '/';
            fileManager.selectedFile = null;
            fileManager.selectedFiles.clear();
        }
        
        if (window.uploadManager) {
            uploadManager.clearUploadQueue();
        }
        
        // 跳转到登录页面
        window.location.href = 'login.html';
    }

    // 获取当前用户
    getCurrentUser() {
        return this.currentUser;
    }

    // 检查是否已登录
    isUserLoggedIn() {
        return this.isLoggedIn;
    }

    // 初始化认证状态
    initAuth() {
        // 检查当前页面
        const currentPage = window.location.pathname.split('/').pop();
        
        if (currentPage === 'login.html' || currentPage === '') {
            // 在登录页面，检查是否已登录
            if (this.checkLoginStatus()) {
                // 已登录，跳转到主界面
                window.location.href = 'dashboard.html';
            }
        } else if (currentPage === 'dashboard.html') {
            // 在主界面，检查是否已登录
            if (!this.checkLoginStatus()) {
                // 未登录，跳转到登录页面
                window.location.href = 'login.html';
            } else {
                // 已登录，初始化主界面
                this.initDashboard();
            }
        }
    }

    // 初始化主界面
    initDashboard() {
        if (!this.isLoggedIn) return;

        // 设置用户信息显示
        const userNameElement = document.getElementById('userName');
        const userAvatarElement = document.getElementById('userAvatar');
        
        if (userNameElement) {
            userNameElement.textContent = this.currentUser;
        }
        
        if (userAvatarElement) {
            userAvatarElement.textContent = this.currentUser.charAt(0).toUpperCase();
        }

        // 设置全局状态
        window.globalState = {
            currentUser: this.currentUser,
            isLoggedIn: this.isLoggedIn,
            currentPath: '/'
        };

        // 初始化各个模块
        if (window.fileManager) {
            fileManager.init(this.currentUser);
        }

        if (window.uploadManager) {
            uploadManager.initializeDragUpload();
        }

        // 启用按钮
        UI.enableButtons();

        // 加载文件列表
        if (window.fileManager) {
            fileManager.refreshList();
        }

        UI.setStatus('欢迎回来，' + this.currentUser + '！');
    }

    // 验证用户名格式
    validateUsername(username) {
        if (!username || username.trim().length === 0) {
            return { valid: false, message: '用户名不能为空' };
        }
        
        const trimmed = username.trim();
        
        if (trimmed.length < 3) {
            return { valid: false, message: '用户名至少需要3个字符' };
        }
        
        if (trimmed.length > 20) {
            return { valid: false, message: '用户名不能超过20个字符' };
        }
        
        // 检查是否包含特殊字符
        const validPattern = /^[a-zA-Z0-9_-]+$/;
        if (!validPattern.test(trimmed)) {
            return { valid: false, message: '用户名只能包含字母、数字、下划线和连字符' };
        }
        
        return { valid: true, message: '用户名格式正确' };
    }

    // 验证密码格式
    validatePassword(password) {
        if (!password || password.length === 0) {
            return { valid: false, message: '密码不能为空' };
        }
        
        if (password.length < 6) {
            return { valid: false, message: '密码至少需要6个字符' };
        }
        
        if (password.length > 50) {
            return { valid: false, message: '密码不能超过50个字符' };
        }
        
        return { valid: true, message: '密码格式正确' };
    }

    // 自动登录（如果有保存的凭据）
    autoLogin() {
        return this.checkLoginStatus();
    }

    // 获取用户头像字母
    getUserAvatar() {
        if (!this.currentUser) return '?';
        return this.currentUser.charAt(0).toUpperCase();
    }

    // 获取用户显示名称
    getUserDisplayName() {
        return this.currentUser || '未登录';
    }
}

// 创建全局实例
const authManager = new AuthManager();

// 页面加载时自动初始化认证
document.addEventListener('DOMContentLoaded', function() {
    authManager.initAuth();
});
