/**
 * 登录页面JavaScript模块
 */

class LoginPage {
    constructor() {
        this.isLoading = false;
        this.init();
    }

    // 初始化登录页面
    init() {
        // 绑定表单提交事件
        const loginForm = document.getElementById('loginForm');
        if (loginForm) {
            loginForm.addEventListener('submit', (e) => {
                e.preventDefault();
                this.handleLogin();
            });
        }

        // 绑定回车键登录
        document.addEventListener('keypress', (e) => {
            if (e.key === 'Enter' && !this.isLoading) {
                this.handleLogin();
            }
        });

        // 绑定注册按钮
        window.showRegister = () => this.handleRegister();
    }

    // 处理登录
    async handleLogin() {
        if (this.isLoading) return;

        const username = document.getElementById('username').value.trim();
        const password = document.getElementById('password').value;

        // 验证输入
        const usernameValidation = authManager.validateUsername(username);
        if (!usernameValidation.valid) {
            this.showMessage(usernameValidation.message, 'error');
            return;
        }

        const passwordValidation = authManager.validatePassword(password);
        if (!passwordValidation.valid) {
            this.showMessage(passwordValidation.message, 'error');
            return;
        }

        this.showLoading(true);
        this.hideMessage();

        try {
            const result = await authManager.login(username, password);
            
            if (result.success) {
                this.showMessage('登录成功，正在跳转...', 'success');
                
                // 延迟跳转，让用户看到成功消息
                setTimeout(() => {
                    window.location.href = 'dashboard.html';
                }, 1000);
            } else {
                this.showMessage('登录失败: ' + result.message, 'error');
            }
        } catch (error) {
            this.showMessage('登录失败: ' + error.message, 'error');
        } finally {
            this.showLoading(false);
        }
    }

    // 处理注册
    async handleRegister() {
        if (this.isLoading) return;

        const username = prompt('请输入要注册的用户名:');
        if (!username || username.trim() === '') {
            return;
        }

        const trimmedUsername = username.trim();
        
        // 验证用户名
        const usernameValidation = authManager.validateUsername(trimmedUsername);
        if (!usernameValidation.valid) {
            alert(usernameValidation.message);
            return;
        }

        const password = prompt('请输入密码:');
        if (!password || password.trim() === '') {
            return;
        }

        // 验证密码
        const passwordValidation = authManager.validatePassword(password);
        if (!passwordValidation.valid) {
            alert(passwordValidation.message);
            return;
        }

        this.showLoading(true);
        this.hideMessage();

        try {
            const result = await authManager.register(trimmedUsername, password);
            
            if (result.success) {
                this.showMessage('注册成功！请使用新账户登录', 'success');
                // 自动填入用户名
                document.getElementById('username').value = trimmedUsername;
                document.getElementById('password').value = '';
                document.getElementById('password').focus();
            } else {
                this.showMessage('注册失败: ' + result.message, 'error');
            }
        } catch (error) {
            this.showMessage('注册失败: ' + error.message, 'error');
        } finally {
            this.showLoading(false);
        }
    }

    // 显示加载状态
    showLoading(show) {
        this.isLoading = show;
        
        const loading = document.getElementById('loading');
        const loginBtn = document.getElementById('loginBtn');
        
        if (loading && loginBtn) {
            if (show) {
                loading.style.display = 'block';
                loginBtn.disabled = true;
                loginBtn.textContent = '登录中...';
            } else {
                loading.style.display = 'none';
                loginBtn.disabled = false;
                loginBtn.textContent = '登录';
            }
        }
    }

    // 显示消息
    showMessage(message, type) {
        const messageDiv = document.getElementById('statusMessage');
        if (messageDiv) {
            messageDiv.textContent = message;
            messageDiv.className = 'status-message status-' + type;
            messageDiv.style.display = 'block';
        }
    }

    // 隐藏消息
    hideMessage() {
        const messageDiv = document.getElementById('statusMessage');
        if (messageDiv) {
            messageDiv.style.display = 'none';
        }
    }

    // 清空表单
    clearForm() {
        const usernameInput = document.getElementById('username');
        const passwordInput = document.getElementById('password');
        
        if (usernameInput) usernameInput.value = '';
        if (passwordInput) passwordInput.value = '';
        
        this.hideMessage();
    }

    // 设置焦点到用户名输入框
    focusUsername() {
        const usernameInput = document.getElementById('username');
        if (usernameInput) {
            usernameInput.focus();
        }
    }

    // 设置焦点到密码输入框
    focusPassword() {
        const passwordInput = document.getElementById('password');
        if (passwordInput) {
            passwordInput.focus();
        }
    }
}

// 页面加载完成后初始化
document.addEventListener('DOMContentLoaded', function() {
    // 创建登录页面实例
    window.loginPage = new LoginPage();
    
    // 设置焦点到用户名输入框
    setTimeout(() => {
        loginPage.focusUsername();
    }, 100);
});
