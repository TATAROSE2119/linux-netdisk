/**
 * 国际化管理器
 * 负责多语言支持和切换
 */

class I18nManager {
    constructor() {
        this.currentLanguage = 'zh';
        this.translations = {};
        this.init();
    }

    // 初始化国际化系统
    init() {
        // 从localStorage获取保存的语言设置
        const savedLanguage = localStorage.getItem('netdisk_language');
        if (savedLanguage && ['zh', 'en'].includes(savedLanguage)) {
            this.currentLanguage = savedLanguage;
        } else {
            // 根据浏览器语言自动选择
            const browserLang = navigator.language || navigator.userLanguage;
            this.currentLanguage = browserLang.startsWith('zh') ? 'zh' : 'en';
        }

        this.loadTranslations();
        this.setupLanguageToggle();
    }

    // 加载翻译数据
    loadTranslations() {
        this.translations = {
            zh: {
                // 通用
                'loading': '加载中...',
                'success': '成功',
                'error': '错误',
                'confirm': '确认',
                'cancel': '取消',
                'close': '关闭',
                'save': '保存',
                'delete': '删除',
                'edit': '编辑',
                'back': '返回',
                'next': '下一步',
                'previous': '上一步',
                'refresh': '刷新',
                'search': '搜索',
                'select': '选择',
                'upload': '上传',
                'download': '下载',
                'rename': '重命名',
                'create': '创建',
                'login': '登录',
                'logout': '退出登录',
                'register': '注册',
                'username': '用户名',
                'password': '密码',
                'file': '文件',
                'folder': '文件夹',
                'size': '大小',
                'type': '类型',
                'modified': '修改时间',
                'actions': '操作',
                'name': '名称',

                // 导航栏
                'navbar.title': '网盘系统',
                'navbar.logout': '🚪 退出登录',

                // 登录页面
                'login.title': '网盘系统',
                'login.subtitle': '安全、便捷的文件存储与管理',
                'login.username.placeholder': '请输入用户名',
                'login.password.placeholder': '请输入密码',
                'login.button': '登录',
                'login.register.button': '注册新账户',
                'login.footer': '© 2024 网盘系统. 保留所有权利.',

                // 工具栏
                'toolbar.upload': '📤 上传文件',
                'toolbar.batch.upload': '📤 批量上传',
                'toolbar.download': '📥 下载文件',
                'toolbar.batch.download': '📦 批量下载',
                'toolbar.rename': '✏️ 重命名',
                'toolbar.delete': '🗑️ 删除',
                'toolbar.create.folder': '📁 新建文件夹',
                'toolbar.refresh': '🔄 刷新列表',
                'toolbar.select.all': '☑️ 全选',
                'toolbar.clear.selection': '❌ 取消选择',

                // 导航
                'nav.back': '⬅️ 返回上级',
                'nav.current.path': '📍 当前位置:',

                // 上传区域
                'upload.area.title': '📁 拖拽文件到此处上传',
                'upload.area.subtitle': '或点击上传按钮选择文件',
                'upload.area.path': '将上传到:',

                // 文件列表
                'filelist.select': '☑️ 选择',
                'filelist.name': '📄 名称',
                'filelist.type': '📂 类型',
                'filelist.size': '📊 大小',
                'filelist.modified': '📅 修改时间',
                'filelist.actions': '⚙️ 操作',
                'filelist.select.button': '选择',
                'filelist.enter.button': '进入',

                // 状态和消息
                'status.ready': '就绪',
                'status.selected': '已选择',
                'status.files': '个文件',
                'status.uploading': '正在上传',
                'status.downloading': '正在下载',
                'status.deleting': '正在删除',
                'status.renaming': '正在重命名',
                'status.creating': '正在创建',

                // 对话框和提示
                'dialog.delete.confirm': '确定要删除吗？此操作不可恢复！',
                'dialog.rename.prompt': '请输入新名称:',
                'dialog.create.folder.prompt': '请输入文件夹名称:',
                'alert.login.required': '请先登录！',
                'alert.select.file.first': '请先选择文件',
                'alert.select.folder.first': '请先选择文件夹',
                'alert.no.files.selected': '请先选择要操作的文件',

                // 上传队列
                'upload.queue.title': '📤 上传队列',
                'upload.queue.clear': '🗑️ 清空队列',
                'upload.queue.pause': '⏸️ 暂停',
                'upload.queue.resume': '▶️ 继续',

                // 移动端提示
                'mobile.scroll.hint': '👈 左右滑动查看更多',

                // 语言切换
                'language.switch': '🌐 EN',
                'language.current': '中文'
            },
            en: {
                // Common
                'loading': 'Loading...',
                'success': 'Success',
                'error': 'Error',
                'confirm': 'Confirm',
                'cancel': 'Cancel',
                'close': 'Close',
                'save': 'Save',
                'delete': 'Delete',
                'edit': 'Edit',
                'back': 'Back',
                'next': 'Next',
                'previous': 'Previous',
                'refresh': 'Refresh',
                'search': 'Search',
                'select': 'Select',
                'upload': 'Upload',
                'download': 'Download',
                'rename': 'Rename',
                'create': 'Create',
                'login': 'Login',
                'logout': 'Logout',
                'register': 'Register',
                'username': 'Username',
                'password': 'Password',
                'file': 'File',
                'folder': 'Folder',
                'size': 'Size',
                'type': 'Type',
                'modified': 'Modified',
                'actions': 'Actions',
                'name': 'Name',

                // Navbar
                'navbar.title': 'NetDisk System',
                'navbar.logout': '🚪 Logout',

                // Login page
                'login.title': 'NetDisk System',
                'login.subtitle': 'Secure and convenient file storage & management',
                'login.username.placeholder': 'Enter username',
                'login.password.placeholder': 'Enter password',
                'login.button': 'Login',
                'login.register.button': 'Register New Account',
                'login.footer': '© 2024 NetDisk System. All rights reserved.',

                // Toolbar
                'toolbar.upload': '📤 Upload File',
                'toolbar.batch.upload': '📤 Batch Upload',
                'toolbar.download': '📥 Download File',
                'toolbar.batch.download': '📦 Batch Download',
                'toolbar.rename': '✏️ Rename',
                'toolbar.delete': '🗑️ Delete',
                'toolbar.create.folder': '📁 New Folder',
                'toolbar.refresh': '🔄 Refresh List',
                'toolbar.select.all': '☑️ Select All',
                'toolbar.clear.selection': '❌ Clear Selection',

                // Navigation
                'nav.back': '⬅️ Back',
                'nav.current.path': '📍 Current Path:',

                // Upload area
                'upload.area.title': '📁 Drag files here to upload',
                'upload.area.subtitle': 'Or click upload button to select files',
                'upload.area.path': 'Upload to:',

                // File list
                'filelist.select': '☑️ Select',
                'filelist.name': '📄 Name',
                'filelist.type': '📂 Type',
                'filelist.size': '📊 Size',
                'filelist.modified': '📅 Modified',
                'filelist.actions': '⚙️ Actions',
                'filelist.select.button': 'Select',
                'filelist.enter.button': 'Enter',

                // Status and messages
                'status.ready': 'Ready',
                'status.selected': 'Selected',
                'status.files': 'files',
                'status.uploading': 'Uploading',
                'status.downloading': 'Downloading',
                'status.deleting': 'Deleting',
                'status.renaming': 'Renaming',
                'status.creating': 'Creating',

                // Dialogs and alerts
                'dialog.delete.confirm': 'Are you sure you want to delete? This action cannot be undone!',
                'dialog.rename.prompt': 'Enter new name:',
                'dialog.create.folder.prompt': 'Enter folder name:',
                'alert.login.required': 'Please login first!',
                'alert.select.file.first': 'Please select a file first',
                'alert.select.folder.first': 'Please select a folder first',
                'alert.no.files.selected': 'Please select files to operate',

                // Upload queue
                'upload.queue.title': '📤 Upload Queue',
                'upload.queue.clear': '🗑️ Clear Queue',
                'upload.queue.pause': '⏸️ Pause',
                'upload.queue.resume': '▶️ Resume',

                // Mobile hints
                'mobile.scroll.hint': '👈 Swipe to see more',

                // Language switch
                'language.switch': '🌐 中文',
                'language.current': 'English'
            }
        };
    }

    // 获取翻译文本
    t(key, params = {}) {
        const translation = this.translations[this.currentLanguage]?.[key] || key;
        
        // 支持参数替换
        return translation.replace(/\{\{(\w+)\}\}/g, (match, param) => {
            return params[param] || match;
        });
    }

    // 切换语言
    switchLanguage(language = null) {
        if (language) {
            this.currentLanguage = language;
        } else {
            // 切换到另一种语言
            this.currentLanguage = this.currentLanguage === 'zh' ? 'en' : 'zh';
        }

        // 保存到localStorage
        localStorage.setItem('netdisk_language', this.currentLanguage);

        // 更新界面
        this.updateUI();

        // 触发语言切换事件
        window.dispatchEvent(new CustomEvent('languageChanged', {
            detail: { language: this.currentLanguage }
        }));
    }

    // 获取当前语言
    getCurrentLanguage() {
        return this.currentLanguage;
    }

    // 设置语言切换按钮
    setupLanguageToggle() {
        // 在页面加载完成后设置
        document.addEventListener('DOMContentLoaded', () => {
            this.createLanguageToggleButton();
            this.updateUI();
        });
    }

    // 创建语言切换按钮
    createLanguageToggleButton() {
        const navbar = document.querySelector('.navbar-right');
        if (navbar) {
            const langButton = document.createElement('button');
            langButton.id = 'languageToggle';
            langButton.className = 'logout-btn';
            langButton.style.marginRight = '10px';
            langButton.onclick = () => this.switchLanguage();
            
            // 插入到退出按钮之前
            const logoutBtn = navbar.querySelector('.logout-btn');
            if (logoutBtn) {
                navbar.insertBefore(langButton, logoutBtn);
            } else {
                navbar.appendChild(langButton);
            }
        }
    }

    // 更新界面文本
    updateUI() {
        // 更新所有带有data-i18n属性的元素
        document.querySelectorAll('[data-i18n]').forEach(element => {
            const key = element.getAttribute('data-i18n');
            element.textContent = this.t(key);
        });

        // 更新所有带有data-i18n-placeholder属性的元素
        document.querySelectorAll('[data-i18n-placeholder]').forEach(element => {
            const key = element.getAttribute('data-i18n-placeholder');
            element.placeholder = this.t(key);
        });

        // 更新所有带有data-i18n-title属性的元素
        document.querySelectorAll('[data-i18n-title]').forEach(element => {
            const key = element.getAttribute('data-i18n-title');
            element.title = this.t(key);
        });

        // 更新语言切换按钮
        const langButton = document.getElementById('languageToggle');
        if (langButton) {
            langButton.textContent = this.t('language.switch');
        }

        // 更新页面标题
        const titleKey = document.body.getAttribute('data-page-title');
        if (titleKey) {
            document.title = this.t(titleKey);
        }
    }
}

// 创建全局实例
window.i18n = new I18nManager();
