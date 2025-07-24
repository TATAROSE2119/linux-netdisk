/**
 * 文件管理模块
 * 负责文件的基本操作：上传、下载、删除、重命名等
 */

class FileManager {
    constructor() {
        this.currentUser = '';
        this.currentPath = '/';
        this.selectedFile = null;
        this.selectedFiles = new Set();
    }

    // 初始化文件管理器
    init(username) {
        this.currentUser = username;
        this.updatePathDisplay();

        // 更新全局状态
        if (window.globalState) {
            window.globalState.currentUser = username;
            window.globalState.isLoggedIn = true;
        }
    }

    // 刷新文件列表
    async refreshList() {
        // 获取当前用户
        if (!this.currentUser) {
            if (window.globalState && window.globalState.isLoggedIn) {
                this.currentUser = window.globalState.currentUser;
            } else if (window.authManager) {
                this.currentUser = authManager.getCurrentUser();
            } else {
                this.currentUser = localStorage.getItem('netdisk_user');
            }
        }

        if (!this.currentUser) {
            console.log('[DEBUG] 刷新列表失败：未找到当前用户');
            UI.setStatus('请先登录');
            return;
        }

        console.log('[DEBUG] 刷新列表 - 用户:', this.currentUser, '路径:', this.currentPath);

        UI.setStatus('正在加载文件列表...');
        
        try {
            const response = await fetch(`${window.apiConfig.filesUrl}?username=${this.currentUser}&path=${encodeURIComponent(this.currentPath)}`);
            const data = await response.json();
            
            if (data.success) {
                this.loadFileList(data.files);
                UI.setStatus('文件列表加载完成');
            } else {
                UI.setStatus('加载失败: ' + data.message);
            }
        } catch (error) {
            console.log('加载文件列表失败:', error);
            UI.setStatus('加载文件列表失败');
        }
        
        this.updatePathDisplay();
    }

    // 加载文件列表到表格
    loadFileList(files) {
        const tbody = document.getElementById('fileTableBody');
        tbody.innerHTML = '';

        if (files.length === 0) {
            const row = tbody.insertRow();
            row.innerHTML = `
                <td colspan="6" style="text-align: center; color: #6c757d; padding: 40px;">
                    📁 暂无文件，请上传文件
                </td>
            `;
            return;
        }

        files.forEach(file => {
            const row = tbody.insertRow();
            const isDirectory = file.type === 2;
            const icon = isDirectory ? '📁' : this.getFileIcon(file.name);
            const typeText = isDirectory ? '文件夹' : '文件';
            const sizeText = isDirectory ? '-' : Utils.formatFileSize(file.size);

            row.innerHTML = `
                <td>
                    <input type="checkbox" data-filename="${file.name}"
                           onchange="fileManager.toggleFileSelection('${file.name}', this.checked)"
                           ${isDirectory ? 'disabled title="文件夹不支持批量下载"' : ''}>
                </td>
                <td>
                    <span class="file-icon">${icon}</span>
                    ${isDirectory ? 
                        `<a href="javascript:void(0)" onclick="fileManager.enterFolder('${file.name}')" style="text-decoration: none; color: #007bff;">${file.name}</a>` : 
                        file.name
                    }
                </td>
                <td>${typeText}</td>
                <td>${sizeText}</td>
                <td>${Utils.formatTime(file.mtime)}</td>
                <td>
                    <button class="btn btn-primary" onclick="fileManager.selectItem('${file.name}', ${isDirectory})" style="padding: 5px 10px; font-size: 12px;">选择</button>
                    ${isDirectory ? 
                        `<button class="btn btn-info" onclick="fileManager.enterFolder('${file.name}')" style="margin-left: 5px; padding: 5px 10px; font-size: 12px;">进入</button>` : 
                        ''
                    }
                </td>
            `;
        });
    }

    // 获取文件图标
    getFileIcon(filename) {
        const ext = filename.split('.').pop().toLowerCase();
        const iconMap = {
            'txt': '📄', 'doc': '📄', 'docx': '📄', 'pdf': '📄',
            'jpg': '🖼️', 'jpeg': '🖼️', 'png': '🖼️', 'gif': '🖼️', 'bmp': '🖼️',
            'mp3': '🎵', 'wav': '🎵', 'flac': '🎵', 'aac': '🎵',
            'mp4': '🎬', 'avi': '🎬', 'mkv': '🎬', 'mov': '🎬',
            'zip': '📦', 'rar': '📦', '7z': '📦', 'tar': '📦',
            'exe': '⚙️', 'msi': '⚙️', 'deb': '⚙️', 'rpm': '⚙️'
        };
        return iconMap[ext] || '📄';
    }

    // 选择文件
    selectItem(filename, isDirectory) {
        // 清除之前的选择
        const rows = document.querySelectorAll('#fileTableBody tr');
        rows.forEach(row => row.classList.remove('selected'));
        
        // 选择当前行
        event.target.closest('tr').classList.add('selected');
        this.selectedFile = filename;
        
        UI.setStatus('已选择: ' + filename + (isDirectory ? ' (文件夹)' : ' (文件)'));
    }

    // 进入文件夹
    enterFolder(folderName) {
        if (this.currentPath === '/') {
            this.currentPath = '/' + folderName;
        } else {
            this.currentPath = this.currentPath + '/' + folderName;
        }

        // 更新全局状态
        if (window.globalState) {
            window.globalState.currentPath = this.currentPath;
        }

        this.updatePathDisplay();
        this.refreshList();
        this.selectedFile = null;
        this.clearSelection();
    }

    // 返回上级目录
    goBack() {
        if (this.currentPath === '/') {
            return;
        }

        const pathParts = this.currentPath.split('/');
        pathParts.pop();
        this.currentPath = pathParts.join('/') || '/';

        // 更新全局状态
        if (window.globalState) {
            window.globalState.currentPath = this.currentPath;
        }

        this.updatePathDisplay();
        this.refreshList();
        this.selectedFile = null;
        this.clearSelection();
    }

    // 更新路径显示
    updatePathDisplay() {
        const pathElement = document.getElementById('currentPathDisplay');
        const uploadPathElement = document.getElementById('uploadPathDisplay');
        if (pathElement) pathElement.textContent = this.currentPath;
        if (uploadPathElement) uploadPathElement.textContent = this.currentPath;
        
        // 更新导航按钮状态
        const backBtn = document.getElementById('backBtn');
        if (backBtn) {
            backBtn.disabled = (this.currentPath === '/');
        }
    }

    // 切换文件选择状态
    toggleFileSelection(filename, isSelected) {
        console.log('[DEBUG] 切换文件选择:', filename, '选中:', isSelected);

        if (isSelected) {
            this.selectedFiles.add(filename);
        } else {
            this.selectedFiles.delete(filename);
        }

        console.log('[DEBUG] 当前选中文件:', Array.from(this.selectedFiles));
        this.updateSelectionStatus();
    }

    // 全选文件
    selectAll() {
        const checkboxes = document.querySelectorAll('#fileTableBody input[type="checkbox"]:not([disabled])');
        checkboxes.forEach(checkbox => {
            checkbox.checked = true;
            const filename = checkbox.getAttribute('onchange').match(/'([^']+)'/)[1];
            this.selectedFiles.add(filename);
        });
        this.updateSelectionStatus();
    }

    // 取消所有选择
    clearSelection() {
        const checkboxes = document.querySelectorAll('#fileTableBody input[type="checkbox"]');
        checkboxes.forEach(checkbox => {
            checkbox.checked = false;
        });
        this.selectedFiles.clear();
        this.updateSelectionStatus();
    }

    // 更新选择状态显示
    updateSelectionStatus() {
        const count = this.selectedFiles.size;
        const batchDownloadBtn = document.getElementById('batchDownloadBtn');
        if (batchDownloadBtn) {
            if (count > 0) {
                UI.setStatus(`已选择 ${count} 个文件`);
                batchDownloadBtn.style.background = '#28a745';
                batchDownloadBtn.textContent = `📦 批量下载 (${count})`;
            } else {
                batchDownloadBtn.style.background = '#6c757d';
                batchDownloadBtn.textContent = '📦 批量下载';
            }
        }
    }

    // 删除文件或文件夹
    async deleteItem() {
        // 获取当前用户
        let currentUser = this.currentUser;
        if (!currentUser) {
            if (window.globalState && window.globalState.isLoggedIn) {
                currentUser = window.globalState.currentUser;
            } else if (window.authManager) {
                currentUser = authManager.getCurrentUser();
            } else {
                currentUser = localStorage.getItem('netdisk_user');
            }
        }

        if (!currentUser) {
            alert('请先登录');
            return;
        }

        if (!this.selectedFile) {
            alert('请先选择要删除的文件或文件夹');
            return;
        }

        if (!confirm('确定要删除 "' + this.selectedFile + '" 吗？此操作不可恢复！')) {
            return;
        }

        UI.setStatus('正在删除: ' + this.selectedFile);

        const filePath = this.currentPath === '/' ? '/' + this.selectedFile : this.currentPath + '/' + this.selectedFile;
        
        try {
            const response = await fetch(window.apiConfig.deleteUrl, {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
                body: JSON.stringify({
                    username: currentUser,
                    filePath: filePath
                })
            });
            
            const data = await response.json();
            if (data.success) {
                UI.setStatus('删除成功: ' + this.selectedFile);
                this.selectedFile = null;
                this.refreshList();
            } else {
                UI.setStatus('删除失败: ' + data.message);
            }
        } catch (error) {
            console.log('删除失败:', error);
            UI.setStatus('删除失败: ' + error.message);
        }
    }

    // 新建文件夹
    async createFolder() {
        // 获取当前用户
        let currentUser = this.currentUser;
        if (!currentUser) {
            if (window.globalState && window.globalState.isLoggedIn) {
                currentUser = window.globalState.currentUser;
            } else if (window.authManager) {
                currentUser = authManager.getCurrentUser();
            } else {
                currentUser = localStorage.getItem('netdisk_user');
            }
        }

        if (!currentUser) {
            alert('请先登录');
            return;
        }

        const folderName = prompt('请输入文件夹名称:');
        if (!folderName || folderName.trim() === '') {
            return;
        }

        const trimmedName = folderName.trim();
        if (trimmedName.includes('/') || trimmedName.includes('\\')) {
            alert('文件夹名称不能包含 / 或 \\ 字符');
            return;
        }

        UI.setStatus('正在创建文件夹: ' + trimmedName);

        const folderPath = this.currentPath === '/' ? '/' + trimmedName : this.currentPath + '/' + trimmedName;

        console.log('[DEBUG] 创建文件夹 - 用户:', currentUser, '路径:', folderPath);

        try {
            const response = await fetch(window.apiConfig.mkdirUrl, {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
                body: JSON.stringify({
                    username: currentUser,
                    path: folderPath
                })
            });
            
            const data = await response.json();
            if (data.success) {
                UI.setStatus('文件夹创建成功: ' + trimmedName);
                this.refreshList();
            } else {
                UI.setStatus('文件夹创建失败: ' + data.message);
            }
        } catch (error) {
            console.log('创建文件夹失败:', error);
            UI.setStatus('文件夹创建失败: ' + error.message);
        }
    }

    // 重命名文件或文件夹
    async renameItem() {
        if (!this.selectedFile) {
            alert('请先选择要重命名的文件或文件夹');
            return;
        }
        
        const newName = prompt('请输入新名称:', this.selectedFile);
        if (!newName || newName.trim() === '' || newName.trim() === this.selectedFile) {
            return;
        }
        
        const trimmedName = newName.trim();
        if (trimmedName.includes('/') || trimmedName.includes('\\')) {
            alert('名称不能包含 / 或 \\ 字符');
            return;
        }
        
        UI.setStatus('正在重命名: ' + this.selectedFile + ' -> ' + trimmedName);
        
        // 构建旧路径和新路径
        const oldPath = this.currentPath === '/' ? '/' + this.selectedFile : this.currentPath + '/' + this.selectedFile;
        const newPath = this.currentPath === '/' ? '/' + trimmedName : this.currentPath + '/' + trimmedName;
        
        try {
            const response = await fetch(window.apiConfig.renameUrl, {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
                body: JSON.stringify({
                    username: this.currentUser,
                    oldPath: oldPath,
                    newPath: newPath
                })
            });
            
            const data = await response.json();
            if (data.success) {
                UI.setStatus('重命名成功: ' + this.selectedFile + ' -> ' + trimmedName);
                this.selectedFile = null;
                this.refreshList();
            } else {
                UI.setStatus('重命名失败: ' + data.message);
            }
        } catch (error) {
            console.log('重命名失败:', error);
            UI.setStatus('重命名失败: ' + error.message);
        }
    }
}

// 创建全局实例
const fileManager = new FileManager();
