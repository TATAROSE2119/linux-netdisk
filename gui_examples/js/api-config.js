/**
 * API配置文件
 * 自动检测当前服务器地址和端口
 */

class ApiConfig {
    constructor() {
        this.baseUrl = this.detectBaseUrl();
        console.log('[API] 使用API基础地址:', this.baseUrl);
    }

    // 自动检测API基础地址
    detectBaseUrl() {
        const protocol = window.location.protocol;
        const hostname = window.location.hostname;
        const port = window.location.port;
        
        // 如果有端口号，包含端口号
        if (port) {
            return `${protocol}//${hostname}:${port}`;
        } else {
            return `${protocol}//${hostname}`;
        }
    }

    // 获取完整的API URL
    getApiUrl(endpoint) {
        return `${this.baseUrl}/api${endpoint}`;
    }

    // 常用API端点
    get loginUrl() {
        return this.getApiUrl('/login');
    }

    get registerUrl() {
        return this.getApiUrl('/register');
    }

    get filesUrl() {
        return this.getApiUrl('/files');
    }

    get uploadUrl() {
        return this.getApiUrl('/upload');
    }

    get downloadUrl() {
        return this.getApiUrl('/download');
    }

    get batchDownloadUrl() {
        return this.getApiUrl('/batch-download');
    }

    get deleteUrl() {
        return this.getApiUrl('/delete');
    }

    get mkdirUrl() {
        return this.getApiUrl('/mkdir');
    }

    get renameUrl() {
        return this.getApiUrl('/rename');
    }

    get healthUrl() {
        return this.getApiUrl('/health');
    }
}

// 创建全局API配置实例
window.apiConfig = new ApiConfig();
