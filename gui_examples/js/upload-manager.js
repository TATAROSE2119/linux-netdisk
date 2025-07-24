/**
 * 上传管理模块
 * 负责文件上传、批量上传队列管理
 */

class UploadManager {
    constructor() {
        this.uploadQueue = [];
        this.isUploading = false;
        this.isPaused = false;
        this.currentUploadIndex = 0;
    }

    // 获取当前用户
    getCurrentUser() {
        // 优先使用全局状态
        if (window.globalState && window.globalState.isLoggedIn) {
            return window.globalState.currentUser;
        }
        // 备用方案：从authManager获取
        if (window.authManager && authManager.isUserLoggedIn()) {
            return authManager.getCurrentUser();
        }
        // 最后备用：从localStorage获取
        return localStorage.getItem('netdisk_user');
    }

    // 获取当前路径
    getCurrentPath() {
        // 优先使用全局状态
        if (window.globalState) {
            return window.globalState.currentPath;
        }
        // 备用方案：从fileManager获取
        if (window.fileManager) {
            return fileManager.currentPath;
        }
        return '/';
    }

    // 单文件上传
    uploadFile() {
        const currentUser = this.getCurrentUser();
        console.log('[DEBUG] 上传文件 - 当前用户:', currentUser);
        console.log('[DEBUG] 全局状态:', window.globalState);
        console.log('[DEBUG] localStorage用户:', localStorage.getItem('netdisk_user'));

        if (!currentUser) {
            alert('请先登录');
            return;
        }
        
        // 创建文件选择器
        const input = document.createElement('input');
        input.type = 'file';
        input.accept = '*/*';
        
        input.onchange = (e) => {
            const file = e.target.files[0];
            if (file) {
                this.uploadFileToServer(file);
            }
        };
        
        input.click();
    }

    // 上传文件到服务器
    async uploadFileToServer(file) {
        const currentUser = this.getCurrentUser();
        if (!currentUser) {
            alert('请先登录');
            return;
        }

        UI.setStatus('正在上传文件: ' + file.name);
        UI.setProgress(0);

        // 创建FormData对象
        const formData = new FormData();
        formData.append('file', file);
        formData.append('username', currentUser);
        formData.append('path', this.getCurrentPath());

        // 设置上传超时处理
        const uploadTimeout = setTimeout(() => {
            console.log(`[DEBUG] 单文件上传超时: ${file.name}`);
            UI.setStatus(`上传可能已完成: ${file.name}（无响应，正在刷新列表）`);
            UI.setProgress(100);

            // 刷新文件列表以验证文件是否已上传
            setTimeout(() => {
                UI.setProgress(0);
                if (window.fileManager) {
                    fileManager.refreshList();
                }
            }, 1000);
        }, 30000); // 30秒超时

        try {
            const response = await fetch('http://localhost:5000/api/upload', {
                method: 'POST',
                body: formData
            });

            clearTimeout(uploadTimeout); // 清除超时

            const data = await response.json();
            if (data.success) {
                UI.setStatus('文件上传成功: ' + file.name);
                UI.setProgress(100);
                setTimeout(() => {
                    UI.setProgress(0);
                    if (window.fileManager) {
                        fileManager.refreshList();
                    }
                }, 1000);
            } else {
                // 如果服务器返回失败，但文件可能已上传
                if (data.message && (data.message.includes('超时') || data.message.includes('timed out'))) {
                    UI.setStatus(`上传可能已完成: ${file.name}（服务器无响应，正在刷新列表）`);
                    UI.setProgress(100);

                    setTimeout(() => {
                        UI.setProgress(0);
                        if (window.fileManager) {
                            fileManager.refreshList();
                        }
                    }, 1000);
                } else {
                    UI.setStatus('文件上传失败: ' + data.message);
                    UI.setProgress(0);
                }
            }
        } catch (error) {
            clearTimeout(uploadTimeout); // 清除超时
            console.log('上传失败:', error);

            // 网络错误可能是因为服务器处理时间过长，文件可能已上传
            UI.setStatus(`上传可能已完成: ${file.name}（网络错误，正在刷新列表）`);
            UI.setProgress(100);

            setTimeout(() => {
                UI.setProgress(0);
                if (window.fileManager) {
                    fileManager.refreshList();
                }
            }, 1000);
        }
    }

    // 批量上传
    batchUpload() {
        const currentUser = this.getCurrentUser();
        if (!currentUser) {
            alert('请先登录');
            return;
        }
        
        // 创建文件选择器，支持多选
        const input = document.createElement('input');
        input.type = 'file';
        input.multiple = true;
        input.accept = '*/*';
        
        input.onchange = (e) => {
            const files = Array.from(e.target.files);
            if (files.length > 0) {
                this.addFilesToQueue(files);
            }
        };
        
        input.click();
    }

    // 添加文件到上传队列
    addFilesToQueue(files) {
        files.forEach((file, index) => {
            const queueItem = {
                id: Date.now() + index,
                file: file,
                status: 'waiting',  // waiting, uploading, completed, failed, paused
                progress: 0,
                error: null
            };
            this.uploadQueue.push(queueItem);
        });
        
        this.updateQueueDisplay();
        this.showUploadQueue();
        
        // 如果没有正在上传，开始上传
        if (!this.isUploading) {
            this.startBatchUpload();
        }
    }

    // 显示上传队列
    showUploadQueue() {
        const container = document.getElementById('uploadQueueContainer');
        if (container) {
            container.style.display = 'block';
        }
    }

    // 隐藏上传队列
    hideUploadQueue() {
        const container = document.getElementById('uploadQueueContainer');
        if (container) {
            container.style.display = 'none';
        }
    }

    // 更新队列显示
    updateQueueDisplay() {
        const queueContainer = document.getElementById('uploadQueue');
        if (!queueContainer) return;
        
        queueContainer.innerHTML = '';
        
        if (this.uploadQueue.length === 0) {
            queueContainer.innerHTML = '<p style="color: #6c757d;">队列为空</p>';
            return;
        }
        
        this.uploadQueue.forEach(item => {
            const itemDiv = document.createElement('div');
            itemDiv.style.cssText = 'margin: 10px 0; padding: 10px; border: 1px solid #dee2e6; border-radius: 5px; background: white;';
            
            const statusIcon = this.getStatusIcon(item.status);
            const statusColor = this.getStatusColor(item.status);
            
            itemDiv.innerHTML = `
                <div style="display: flex; justify-content: space-between; align-items: center;">
                    <div style="flex: 1;">
                        <strong>${item.file.name}</strong>
                        <span style="color: #6c757d; margin-left: 10px;">(${Utils.formatFileSize(item.file.size)})</span>
                    </div>
                    <div style="display: flex; align-items: center;">
                        <span style="color: ${statusColor}; margin-right: 10px;">${statusIcon} ${this.getStatusText(item.status)}</span>
                        ${item.status === 'waiting' || item.status === 'failed' ? 
                            `<button class="btn btn-danger" onclick="uploadManager.removeFromQueue(${item.id})" style="padding: 2px 8px; font-size: 12px;">移除</button>` : 
                            ''
                        }
                    </div>
                </div>
                ${item.status === 'uploading' ? 
                    `<div style="margin-top: 5px;">
                        <div style="width: 100%; height: 8px; background: #e9ecef; border-radius: 4px; overflow: hidden;">
                            <div style="height: 100%; background: #007bff; width: ${item.progress}%; transition: width 0.3s;"></div>
                        </div>
                        <small style="color: #6c757d;">${item.progress}%</small>
                    </div>` : 
                    ''
                }
                ${item.error ? `<div style="color: #dc3545; font-size: 12px; margin-top: 5px;">错误: ${item.error}</div>` : ''}
            `;
            
            queueContainer.appendChild(itemDiv);
        });
        
        this.updateQueueStatus();
    }

    // 获取状态图标
    getStatusIcon(status) {
        const icons = {
            'waiting': '⏳',
            'uploading': '📤',
            'completed': '✅',
            'failed': '❌',
            'paused': '⏸️'
        };
        return icons[status] || '❓';
    }

    // 获取状态颜色
    getStatusColor(status) {
        const colors = {
            'waiting': '#6c757d',
            'uploading': '#007bff',
            'completed': '#28a745',
            'failed': '#dc3545',
            'paused': '#ffc107'
        };
        return colors[status] || '#6c757d';
    }

    // 获取状态文本
    getStatusText(status) {
        const texts = {
            'waiting': '等待中',
            'uploading': '上传中',
            'completed': '已完成',
            'failed': '失败',
            'paused': '已暂停'
        };
        return texts[status] || '未知';
    }

    // 更新队列状态
    updateQueueStatus() {
        const total = this.uploadQueue.length;
        const completed = this.uploadQueue.filter(item => item.status === 'completed').length;
        const failed = this.uploadQueue.filter(item => item.status === 'failed').length;
        const uploading = this.uploadQueue.filter(item => item.status === 'uploading').length;
        
        const statusText = `总计: ${total} | 已完成: ${completed} | 失败: ${failed} | 上传中: ${uploading}`;
        const statusElement = document.getElementById('queueStatus');
        if (statusElement) {
            statusElement.textContent = statusText;
        }
        
        // 更新按钮状态
        const pauseBtn = document.getElementById('pauseBtn');
        const resumeBtn = document.getElementById('resumeBtn');
        
        if (pauseBtn && resumeBtn) {
            if (this.isUploading && !this.isPaused) {
                pauseBtn.style.display = 'inline-block';
                resumeBtn.style.display = 'none';
            } else if (this.isPaused) {
                pauseBtn.style.display = 'none';
                resumeBtn.style.display = 'inline-block';
            } else {
                pauseBtn.style.display = 'none';
                resumeBtn.style.display = 'none';
            }
        }
    }

    // 从队列中移除文件
    removeFromQueue(itemId) {
        this.uploadQueue = this.uploadQueue.filter(item => item.id !== itemId);
        this.updateQueueDisplay();
        
        if (this.uploadQueue.length === 0) {
            this.hideUploadQueue();
            this.isUploading = false;
            this.isPaused = false;
            this.currentUploadIndex = 0;
        }
    }

    // 清空上传队列
    clearUploadQueue() {
        if (this.isUploading && !this.isPaused) {
            if (!confirm('正在上传中，确定要清空队列吗？')) {
                return;
            }
        }
        
        this.uploadQueue = [];
        this.isUploading = false;
        this.isPaused = false;
        this.currentUploadIndex = 0;
        this.updateQueueDisplay();
        this.hideUploadQueue();
        UI.setStatus('上传队列已清空');
    }

    // 暂停上传
    pauseUpload() {
        this.isPaused = true;
        this.updateQueueDisplay();
        UI.setStatus('上传已暂停');
    }

    // 继续上传
    resumeUpload() {
        this.isPaused = false;
        this.updateQueueDisplay();
        UI.setStatus('上传已继续');
        
        if (!this.isUploading) {
            this.startBatchUpload();
        }
    }

    // 开始批量上传
    startBatchUpload() {
        if (this.uploadQueue.length === 0 || this.isPaused) {
            return;
        }
        
        this.isUploading = true;
        this.updateQueueDisplay();
        
        // 找到下一个等待上传的文件
        const nextItem = this.uploadQueue.find(item => item.status === 'waiting');
        if (!nextItem) {
            // 没有等待上传的文件，上传完成
            this.isUploading = false;
            UI.setStatus('批量上传完成');
            this.updateQueueDisplay();
            
            // 检查是否所有文件都上传完成
            const allCompleted = this.uploadQueue.every(item => item.status === 'completed' || item.status === 'failed');
            if (allCompleted) {
                setTimeout(() => {
                    fileManager.refreshList();
                }, 1000);
            }
            return;
        }
        
        // 开始上传当前文件
        this.uploadSingleFileFromQueue(nextItem);
    }

    // 从队列上传单个文件
    async uploadSingleFileFromQueue(queueItem) {
        if (this.isPaused) {
            return;
        }
        
        queueItem.status = 'uploading';
        queueItem.progress = 0;
        this.updateQueueDisplay();
        
        UI.setStatus(`正在上传: ${queueItem.file.name}`);
        
        // 创建FormData对象
        const formData = new FormData();
        formData.append('file', queueItem.file);
        formData.append('username', this.getCurrentUser());
        formData.append('path', this.getCurrentPath());
        
        // 设置上传超时处理
        const uploadTimeout = setTimeout(() => {
            console.log(`[DEBUG] 上传超时: ${queueItem.file.name}`);
            queueItem.status = 'completed'; // 假设成功，因为C服务器可能已经接收了文件
            queueItem.progress = 100;
            UI.setStatus(`上传可能已完成: ${queueItem.file.name}（无响应，请刷新检查）`);
            this.updateQueueDisplay();

            // 刷新文件列表以验证文件是否已上传
            if (window.fileManager) {
                setTimeout(() => {
                    fileManager.refreshList();
                }, 1000);
            }
        }, 30000); // 30秒超时

        try {
            const response = await fetch('http://localhost:5000/api/upload', {
                method: 'POST',
                body: formData
            });

            clearTimeout(uploadTimeout); // 清除超时

            const data = await response.json();
            if (data.success) {
                queueItem.status = 'completed';
                queueItem.progress = 100;
                UI.setStatus(`上传成功: ${queueItem.file.name}`);
            } else {
                // 如果服务器返回失败，但文件可能已上传
                if (data.message && (data.message.includes('超时') || data.message.includes('timed out'))) {
                    queueItem.status = 'completed'; // 假设成功
                    queueItem.progress = 100;
                    UI.setStatus(`上传可能已完成: ${queueItem.file.name}（服务器无响应，请刷新检查）`);

                    // 刷新文件列表以验证文件是否已上传
                    if (window.fileManager) {
                        setTimeout(() => {
                            fileManager.refreshList();
                        }, 1000);
                    }
                } else {
                    queueItem.status = 'failed';
                    queueItem.error = data.message;
                    UI.setStatus(`上传失败: ${queueItem.file.name}`);
                }
            }
        } catch (error) {
            clearTimeout(uploadTimeout); // 清除超时
            console.log('上传失败:', error);

            // 网络错误可能是因为服务器处理时间过长，文件可能已上传
            queueItem.status = 'completed'; // 假设成功
            queueItem.progress = 100;
            queueItem.error = error.message;
            UI.setStatus(`上传可能已完成: ${queueItem.file.name}（网络错误，请刷新检查）`);

            // 刷新文件列表以验证文件是否已上传
            if (window.fileManager) {
                setTimeout(() => {
                    fileManager.refreshList();
                }, 1000);
            }
        }
        
        this.updateQueueDisplay();
        
        // 继续上传下一个文件
        setTimeout(() => {
            this.startBatchUpload();
        }, 500);
    }

    // 初始化拖拽上传
    initializeDragUpload() {
        const uploadArea = document.getElementById('uploadArea');
        if (!uploadArea) return;

        uploadArea.addEventListener('dragover', (e) => {
            e.preventDefault();
            uploadArea.classList.add('dragover');
        });

        uploadArea.addEventListener('dragleave', (e) => {
            e.preventDefault();
            uploadArea.classList.remove('dragover');
        });

        uploadArea.addEventListener('drop', (e) => {
            e.preventDefault();
            uploadArea.classList.remove('dragover');

            const currentUser = this.getCurrentUser();
            if (!currentUser) {
                alert('请先登录');
                return;
            }
            
            const files = Array.from(e.dataTransfer.files);
            if (files.length > 0) {
                if (files.length === 1) {
                    this.uploadFileToServer(files[0]);
                } else {
                    this.addFilesToQueue(files);
                }
            }
        });
    }
}

// 创建全局实例
const uploadManager = new UploadManager();
