/**
 * UI Animations Module
 * 提供现代化的UI动效和交互增强
 */

const UIAnimations = {
    // Toast通知队列
    toastQueue: [],
    toastContainer: null,

    /**
     * 初始化动画模块
     */
    init() {
        this.createToastContainer();
        this.initDragDropEffects();
        this.initButtonRipples();
        this.initScrollAnimations();
    },

    /**
     * 创建Toast容器
     */
    createToastContainer() {
        if (document.getElementById('toast-container')) return;

        const container = document.createElement('div');
        container.id = 'toast-container';
        container.style.cssText = `
            position: fixed;
            bottom: 24px;
            right: 24px;
            z-index: 1000;
            display: flex;
            flex-direction: column-reverse;
            gap: 12px;
            pointer-events: none;
        `;
        document.body.appendChild(container);
        this.toastContainer = container;
    },

    /**
     * 显示Toast通知
     * @param {string} message - 消息内容
     * @param {string} type - 类型: success, error, warning, info
     * @param {number} duration - 显示时长(ms)
     */
    showToast(message, type = 'info', duration = 3000) {
        const toast = document.createElement('div');
        toast.className = `toast toast-${type}`;
        toast.style.cssText = `
            background: var(--bg-card, #fff);
            border: 1px solid var(--border-color, #e2e8f0);
            border-radius: var(--radius-md, 10px);
            padding: 16px 20px;
            box-shadow: var(--shadow-xl, 0 20px 25px -5px rgba(0,0,0,0.1));
            display: flex;
            align-items: center;
            gap: 12px;
            animation: toastIn 0.4s cubic-bezier(0.16, 1, 0.3, 1);
            pointer-events: auto;
            min-width: 280px;
            max-width: 400px;
        `;

        const colors = {
            success: 'var(--success, #10b981)',
            error: 'var(--danger, #ef4444)',
            warning: 'var(--warning, #f59e0b)',
            info: 'var(--primary, #6366f1)'
        };

        toast.style.borderLeft = `4px solid ${colors[type]}`;

        const icons = {
            success: '✓',
            error: '✕',
            warning: '!',
            info: 'i'
        };

        toast.innerHTML = `
            <span style="
                width: 24px;
                height: 24px;
                border-radius: 50%;
                background: ${colors[type]};
                color: white;
                display: flex;
                align-items: center;
                justify-content: center;
                font-size: 14px;
                font-weight: bold;
                flex-shrink: 0;
            ">${icons[type]}</span>
            <span style="font-size: 14px; color: var(--text-primary, #0f172a);">${message}</span>
        `;

        this.toastContainer.appendChild(toast);

        // 自动关闭
        setTimeout(() => {
            toast.style.animation = 'toastOut 0.3s ease forwards';
            setTimeout(() => toast.remove(), 300);
        }, duration);

        // 点击关闭
        toast.addEventListener('click', () => {
            toast.style.animation = 'toastOut 0.3s ease forwards';
            setTimeout(() => toast.remove(), 300);
        });
    },

    /**
     * 初始化拖拽效果
     */
    initDragDropEffects() {
        const uploadArea = document.getElementById('uploadArea');
        if (!uploadArea) return;

        uploadArea.addEventListener('dragenter', (e) => {
            e.preventDefault();
            uploadArea.classList.add('dragover');
        });

        uploadArea.addEventListener('dragleave', (e) => {
            e.preventDefault();
            if (!uploadArea.contains(e.relatedTarget)) {
                uploadArea.classList.remove('dragover');
            }
        });

        uploadArea.addEventListener('dragover', (e) => {
            e.preventDefault();
        });

        uploadArea.addEventListener('drop', (e) => {
            e.preventDefault();
            uploadArea.classList.remove('dragover');
        });
    },

    /**
     * 初始化按钮波纹效果
     */
    initButtonRipples() {
        document.addEventListener('click', (e) => {
            const btn = e.target.closest('.btn');
            if (!btn || btn.disabled) return;

            const ripple = document.createElement('span');
            const rect = btn.getBoundingClientRect();
            const size = Math.max(rect.width, rect.height);
            const x = e.clientX - rect.left - size / 2;
            const y = e.clientY - rect.top - size / 2;

            ripple.style.cssText = `
                position: absolute;
                width: ${size}px;
                height: ${size}px;
                left: ${x}px;
                top: ${y}px;
                background: rgba(255, 255, 255, 0.3);
                border-radius: 50%;
                transform: scale(0);
                animation: rippleEffect 0.6s ease-out;
                pointer-events: none;
            `;

            btn.style.position = 'relative';
            btn.style.overflow = 'hidden';
            btn.appendChild(ripple);

            setTimeout(() => ripple.remove(), 600);
        });

        // 添加ripple动画样式
        if (!document.getElementById('ripple-style')) {
            const style = document.createElement('style');
            style.id = 'ripple-style';
            style.textContent = `
                @keyframes rippleEffect {
                    to {
                        transform: scale(4);
                        opacity: 0;
                    }
                }
                @keyframes toastIn {
                    from {
                        opacity: 0;
                        transform: translateY(20px) scale(0.95);
                    }
                    to {
                        opacity: 1;
                        transform: translateY(0) scale(1);
                    }
                }
                @keyframes toastOut {
                    to {
                        opacity: 0;
                        transform: translateY(20px) scale(0.95);
                    }
                }
            `;
            document.head.appendChild(style);
        }
    },

    /**
     * 初始化滚动动画
     */
    initScrollAnimations() {
        const observerOptions = {
            threshold: 0.1,
            rootMargin: '0px 0px -50px 0px'
        };

        const observer = new IntersectionObserver((entries) => {
            entries.forEach(entry => {
                if (entry.isIntersecting) {
                    entry.target.classList.add('animate-in');
                }
            });
        }, observerOptions);

        // 观察需要动画的元素
        document.querySelectorAll('.toolbar, .navigation, .upload-area, .file-list-container, .status-bar').forEach(el => {
            observer.observe(el);
        });
    },

    /**
     * 文件列表加载动画
     * @param {HTMLElement} tbody - 表格body元素
     */
    animateFileList(tbody) {
        const rows = tbody.querySelectorAll('tr');
        rows.forEach((row, index) => {
            row.style.opacity = '0';
            row.style.transform = 'translateX(-10px)';

            setTimeout(() => {
                row.style.transition = 'all 0.3s ease';
                row.style.opacity = '1';
                row.style.transform = 'translateX(0)';
            }, index * 50);
        });
    },

    /**
     * 进度条动画
     * @param {number} percent - 百分比 0-100
     */
    updateProgress(percent) {
        const progressFill = document.getElementById('progressFill');
        if (progressFill) {
            progressFill.style.width = `${percent}%`;
        }
    },

    /**
     * 页面切换动画
     * @param {string} direction - 方向: in, out
     */
    pageTransition(direction = 'in') {
        const container = document.querySelector('.container');
        if (!container) return;

        if (direction === 'out') {
            container.style.opacity = '0';
            container.style.transform = 'translateY(20px)';
        } else {
            container.style.transition = 'all 0.4s ease';
            container.style.opacity = '1';
            container.style.transform = 'translateY(0)';
        }
    },

    /**
     * 震动反馈（用于错误提示）
     * @param {HTMLElement} element - 目标元素
     */
    shake(element) {
        element.style.animation = 'shake 0.5s ease';
        setTimeout(() => {
            element.style.animation = '';
        }, 500);

        // 添加shake动画
        if (!document.getElementById('shake-style')) {
            const style = document.createElement('style');
            style.id = 'shake-style';
            style.textContent = `
                @keyframes shake {
                    0%, 100% { transform: translateX(0); }
                    20% { transform: translateX(-10px); }
                    40% { transform: translateX(10px); }
                    60% { transform: translateX(-10px); }
                    80% { transform: translateX(10px); }
                }
            `;
            document.head.appendChild(style);
        }
    },

    /**
     * 成功动画（打勾效果）
     * @param {HTMLElement} element - 目标元素
     */
    success(element) {
        const original = element.innerHTML;
        element.innerHTML = '✓';
        element.style.color = 'var(--success, #10b981)';
        element.style.transform = 'scale(1.2)';

        setTimeout(() => {
            element.innerHTML = original;
            element.style.color = '';
            element.style.transform = '';
        }, 1500);
    }
};

// 页面加载时初始化
document.addEventListener('DOMContentLoaded', () => {
    UIAnimations.init();
});

// 导出到全局
window.UIAnimations = UIAnimations;
