/**
 * UI工具模块
 * 负责界面状态管理、工具函数等
 */

class UIUtils {
    constructor() {
        // UI工具初始化
    }

    // 设置状态文本
    setStatus(text) {
        const statusElement = document.getElementById('statusText');
        if (statusElement) {
            statusElement.textContent = text;
        }
    }

    // 设置进度条
    setProgress(percent) {
        const progressElement = document.getElementById('progressFill');
        if (progressElement) {
            progressElement.style.width = percent + '%';
        }
    }

    // 启用所有按钮
    enableButtons() {
        const buttons = [
            'uploadBtn', 'batchUploadBtn', 'downloadBtn', 'batchDownloadBtn',
            'deleteBtn', 'createFolderBtn', 'renameBtn', 'refreshBtn',
            'selectAllBtn', 'clearSelectionBtn'
        ];
        
        buttons.forEach(btnId => {
            const btn = document.getElementById(btnId);
            if (btn) btn.disabled = false;
        });
    }

    // 禁用所有按钮
    disableButtons() {
        const buttons = [
            'uploadBtn', 'batchUploadBtn', 'downloadBtn', 'batchDownloadBtn',
            'deleteBtn', 'createFolderBtn', 'renameBtn', 'refreshBtn',
            'selectAllBtn', 'clearSelectionBtn'
        ];
        
        buttons.forEach(btnId => {
            const btn = document.getElementById(btnId);
            if (btn) btn.disabled = true;
        });
    }

    // 显示加载状态
    showLoading(show, message = '加载中...') {
        if (show) {
            this.setStatus(message);
            this.disableButtons();
        } else {
            this.enableButtons();
        }
    }

    // 显示确认对话框
    confirm(message) {
        return window.confirm(message);
    }

    // 显示提示对话框
    alert(message) {
        window.alert(message);
    }

    // 显示输入对话框
    prompt(message, defaultValue = '') {
        return window.prompt(message, defaultValue);
    }

    // 显示成功消息
    showSuccess(message) {
        this.setStatus('✅ ' + message);
        setTimeout(() => {
            this.setStatus('就绪');
        }, 3000);
    }

    // 显示错误消息
    showError(message) {
        this.setStatus('❌ ' + message);
        setTimeout(() => {
            this.setStatus('就绪');
        }, 5000);
    }

    // 显示警告消息
    showWarning(message) {
        this.setStatus('⚠️ ' + message);
        setTimeout(() => {
            this.setStatus('就绪');
        }, 4000);
    }

    // 显示信息消息
    showInfo(message) {
        this.setStatus('ℹ️ ' + message);
        setTimeout(() => {
            this.setStatus('就绪');
        }, 3000);
    }
}

/**
 * 通用工具函数
 */
class Utils {
    // 格式化文件大小
    static formatFileSize(bytes) {
        if (bytes === 0) return '0 B';
        const k = 1024;
        const sizes = ['B', 'KB', 'MB', 'GB', 'TB'];
        const i = Math.floor(Math.log(bytes) / Math.log(k));
        return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
    }

    // 格式化时间
    static formatTime(timestamp) {
        const date = new Date(timestamp * 1000);
        return date.toLocaleString('zh-CN', {
            year: 'numeric',
            month: '2-digit',
            day: '2-digit',
            hour: '2-digit',
            minute: '2-digit',
            second: '2-digit'
        });
    }

    // 获取文件扩展名
    static getFileExtension(filename) {
        return filename.split('.').pop().toLowerCase();
    }

    // 验证文件名
    static validateFileName(filename) {
        const invalidChars = /[<>:"/\\|?*]/;
        return !invalidChars.test(filename);
    }

    // 验证路径
    static validatePath(path) {
        const invalidChars = /[<>:"|?*]/;
        return !invalidChars.test(path);
    }

    // 生成唯一ID
    static generateId() {
        return Date.now().toString(36) + Math.random().toString(36).substr(2);
    }

    // 防抖函数
    static debounce(func, wait) {
        let timeout;
        return function executedFunction(...args) {
            const later = () => {
                clearTimeout(timeout);
                func(...args);
            };
            clearTimeout(timeout);
            timeout = setTimeout(later, wait);
        };
    }

    // 节流函数
    static throttle(func, limit) {
        let inThrottle;
        return function() {
            const args = arguments;
            const context = this;
            if (!inThrottle) {
                func.apply(context, args);
                inThrottle = true;
                setTimeout(() => inThrottle = false, limit);
            }
        };
    }

    // 深拷贝对象
    static deepClone(obj) {
        if (obj === null || typeof obj !== 'object') return obj;
        if (obj instanceof Date) return new Date(obj.getTime());
        if (obj instanceof Array) return obj.map(item => Utils.deepClone(item));
        if (typeof obj === 'object') {
            const clonedObj = {};
            for (const key in obj) {
                if (obj.hasOwnProperty(key)) {
                    clonedObj[key] = Utils.deepClone(obj[key]);
                }
            }
            return clonedObj;
        }
    }

    // 检查是否为空值
    static isEmpty(value) {
        return value === null || value === undefined || value === '' || 
               (Array.isArray(value) && value.length === 0) ||
               (typeof value === 'object' && Object.keys(value).length === 0);
    }

    // 安全的JSON解析
    static safeJsonParse(str, defaultValue = null) {
        try {
            return JSON.parse(str);
        } catch (e) {
            return defaultValue;
        }
    }

    // 格式化路径
    static formatPath(path) {
        // 确保路径以 / 开头
        if (!path.startsWith('/')) {
            path = '/' + path;
        }
        
        // 移除重复的斜杠
        path = path.replace(/\/+/g, '/');
        
        // 移除末尾的斜杠（除非是根路径）
        if (path.length > 1 && path.endsWith('/')) {
            path = path.slice(0, -1);
        }
        
        return path;
    }

    // 获取父路径
    static getParentPath(path) {
        if (path === '/') return '/';
        const parts = path.split('/');
        parts.pop();
        return parts.join('/') || '/';
    }

    // 获取文件名（从路径中）
    static getFileName(path) {
        return path.split('/').pop();
    }

    // 连接路径
    static joinPath(...parts) {
        const path = parts.join('/');
        return Utils.formatPath(path);
    }

    // 检查是否为图片文件
    static isImageFile(filename) {
        const imageExts = ['jpg', 'jpeg', 'png', 'gif', 'bmp', 'webp', 'svg'];
        const ext = Utils.getFileExtension(filename);
        return imageExts.includes(ext);
    }

    // 检查是否为视频文件
    static isVideoFile(filename) {
        const videoExts = ['mp4', 'avi', 'mkv', 'mov', 'wmv', 'flv', 'webm'];
        const ext = Utils.getFileExtension(filename);
        return videoExts.includes(ext);
    }

    // 检查是否为音频文件
    static isAudioFile(filename) {
        const audioExts = ['mp3', 'wav', 'flac', 'aac', 'ogg', 'wma'];
        const ext = Utils.getFileExtension(filename);
        return audioExts.includes(ext);
    }

    // 检查是否为文档文件
    static isDocumentFile(filename) {
        const docExts = ['txt', 'doc', 'docx', 'pdf', 'xls', 'xlsx', 'ppt', 'pptx'];
        const ext = Utils.getFileExtension(filename);
        return docExts.includes(ext);
    }

    // 检查是否为压缩文件
    static isArchiveFile(filename) {
        const archiveExts = ['zip', 'rar', '7z', 'tar', 'gz', 'bz2'];
        const ext = Utils.getFileExtension(filename);
        return archiveExts.includes(ext);
    }
}

// 创建全局实例
const UI = new UIUtils();
