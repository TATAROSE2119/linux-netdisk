/**
 * 下载管理模块
 * 负责文件下载、批量下载功能
 */

class DownloadManager {
    constructor() {
        // 下载管理器初始化
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

    // 下载文件
    downloadFile() {
        if (!fileManager.selectedFile) {
            alert('请选择要下载的文件');
            return;
        }
        
        // 检查是否选择的是文件夹
        const selectedRow = document.querySelector('#fileTableBody tr.selected');
        if (selectedRow) {
            const typeCell = selectedRow.cells[2];
            if (typeCell && typeCell.textContent === '文件夹') {
                alert('不能下载文件夹，请选择文件');
                return;
            }
        }
        
        this.downloadSingleFile(fileManager.selectedFile);
    }

    // 下载单个文件
    async downloadSingleFile(filename) {
        UI.setStatus('正在下载文件: ' + filename);
        UI.setProgress(0);
        
        // 构建文件路径
        const currentPath = this.getCurrentPath();
        const filePath = currentPath === '/' ? '/' + filename : currentPath + '/' + filename;
        
        try {
            const response = await fetch('http://localhost:5000/api/download', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
                body: JSON.stringify({
                    username: this.getCurrentUser(),
                    filePath: filePath
                })
            });
            
            if (response.ok) {
                const blob = await response.blob();
                this.createDownloadLink(blob, filename);
                
                UI.setStatus('文件下载完成: ' + filename);
                UI.setProgress(100);
                setTimeout(() => UI.setProgress(0), 2000);
            } else {
                const data = await response.json();
                throw new Error(data.message || '下载失败');
            }
        } catch (error) {
            console.log('下载失败:', error);
            UI.setStatus('下载失败: ' + error.message);
            UI.setProgress(0);
        }
    }

    // 获取选中的文件
    getSelectedFiles() {
        // 方法1：从全局fileManager获取
        if (window.fileManager && fileManager.selectedFiles) {
            return fileManager.selectedFiles;
        }

        // 方法2：从DOM中获取选中的复选框
        const selectedFiles = new Set();
        const checkboxes = document.querySelectorAll('#fileTableBody input[type="checkbox"]:checked');
        checkboxes.forEach(checkbox => {
            const filename = checkbox.getAttribute('data-filename');
            if (filename) {
                selectedFiles.add(filename);
            }
        });

        return selectedFiles;
    }

    // 批量下载选中的文件
    async downloadSelected() {
        const selectedFiles = this.getSelectedFiles();

        console.log('[DEBUG] 批量下载 - 选中文件数量:', selectedFiles.size);
        console.log('[DEBUG] 选中文件列表:', Array.from(selectedFiles));

        if (selectedFiles.size === 0) {
            alert('请先选择要下载的文件');
            return;
        }

        if (selectedFiles.size === 1) {
            // 如果只选择了一个文件，直接下载
            const filename = Array.from(selectedFiles)[0];
            this.downloadSingleFile(filename);
            return;
        }

        UI.setStatus(`正在打包下载 ${selectedFiles.size} 个文件...`);
        UI.setProgress(0);
        
        // 构建文件路径列表
        const currentPath = this.getCurrentPath();
        const filePaths = Array.from(selectedFiles).map(filename => {
            return currentPath === '/' ? '/' + filename : currentPath + '/' + filename;
        });

        console.log('[DEBUG] 批量下载路径列表:', filePaths);
        
        try {
            const response = await fetch('http://localhost:5000/api/batch-download', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
                body: JSON.stringify({
                    username: this.getCurrentUser(),
                    filePaths: filePaths,
                    zipName: `files_${new Date().getTime()}.zip`
                })
            });
            
            if (response.ok) {
                const blob = await response.blob();
                const filename = `selected_files_${new Date().getTime()}.zip`;
                this.createDownloadLink(blob, filename);
                
                UI.setStatus(`批量下载完成: ${selectedFiles.size} 个文件`);
                UI.setProgress(100);
                setTimeout(() => UI.setProgress(0), 2000);
                
                // 清除选择
                if (window.fileManager) {
                    fileManager.clearSelection();
                }
            } else {
                const data = await response.json();
                throw new Error(data.message || '批量下载失败');
            }
        } catch (error) {
            console.log('批量下载失败:', error);
            UI.setStatus('批量下载失败: ' + error.message);
            UI.setProgress(0);
        }
    }

    // 创建下载链接
    createDownloadLink(blob, filename) {
        const url = window.URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.style.display = 'none';
        a.href = url;
        a.download = filename;
        document.body.appendChild(a);
        a.click();
        window.URL.revokeObjectURL(url);
        document.body.removeChild(a);
    }
}

// 创建全局实例
const downloadManager = new DownloadManager();
