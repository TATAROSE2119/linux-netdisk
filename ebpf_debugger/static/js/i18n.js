/**
 * i18n Module - Internationalization support for eBPF Debugger
 */

// 语言包
const i18nData = {
    en: {
        // Status
        'status.connecting': 'Connecting...',
        'status.connected': 'Connected',
        'status.disconnected': 'Disconnected',
        'status.running': 'Monitoring...',
        'status.stopped': 'Stopped',
        'status.starting': 'Starting...',

        // Buttons
        'btn.start': 'Start',
        'btn.stop': 'Stop',

        // Tabs
        'tab.network': 'Network',
        'tab.syscall': 'Syscalls',
        'tab.perf': 'Performance',

        // Network Panel
        'network.activeConn': 'Active Connections',
        'network.totalSent': 'Total Sent',
        'network.totalRecv': 'Total Received',
        'network.totalConn': 'Total Connections',
        'network.throughput': 'Throughput',
        'network.connections': 'Connections',
        'network.activeConnList': 'Active Connections',
        'network.recentEvents': 'Recent Events',

        // Syscall Panel
        'syscall.distribution': 'Syscall Distribution',
        'syscall.rate': 'Syscall Rate',
        'syscall.topSyscalls': 'Top Syscalls',
        'syscall.fileIO': 'File I/O Statistics',
        'syscall.recentSyscalls': 'Recent Syscalls',

        // Performance Panel
        'perf.ctxSwitches': 'Context Switches',
        'perf.monitoredFuncs': 'Monitored Functions',
        'perf.ctxSwitchRate': 'Context Switch Rate',
        'perf.funcCallTime': 'Function Call Time',
        'perf.funcStats': 'Function Statistics (uprobe)',
        'perf.recentFuncCalls': 'Recent Function Calls',

        // Table Headers
        'table.process': 'Process',
        'table.source': 'Source',
        'table.dest': 'Destination',
        'table.sent': 'Sent',
        'table.recv': 'Received',
        'table.duration': 'Duration',
        'table.syscall': 'Syscall',
        'table.count': 'Count',
        'table.avgTime': 'Avg Time (us)',
        'table.errors': 'Errors',
        'table.errorRate': 'Error Rate',
        'table.file': 'File',
        'table.read': 'Read',
        'table.write': 'Write',
        'table.rCount': 'R Count',
        'table.wCount': 'W Count',
        'table.function': 'Function',
        'table.calls': 'Calls',
        'table.total': 'Total (ms)',
        'table.avg': 'Avg (us)',
        'table.min': 'Min (us)',
        'table.max': 'Max (us)',

        // Chart Labels
        'chart.sent': 'Sent (KB/s)',
        'chart.recv': 'Received (KB/s)',
        'chart.activeConn': 'Active Connections',
        'chart.syscallsPerSec': 'Syscalls/s',
        'chart.switchesPerSec': 'Switches/s',
        'chart.avgTimeUs': 'Avg Time (us)'
    },
    zh: {
        // Status
        'status.connecting': '连接中...',
        'status.connected': '已连接',
        'status.disconnected': '已断开',
        'status.running': '监控中...',
        'status.stopped': '已停止',
        'status.starting': '启动中...',

        // Buttons
        'btn.start': '启动',
        'btn.stop': '停止',

        // Tabs
        'tab.network': '网络',
        'tab.syscall': '系统调用',
        'tab.perf': '性能',

        // Network Panel
        'network.activeConn': '活跃连接',
        'network.totalSent': '总发送',
        'network.totalRecv': '总接收',
        'network.totalConn': '总连接数',
        'network.throughput': '吞吐量',
        'network.connections': '连接数',
        'network.activeConnList': '活跃连接列表',
        'network.recentEvents': '最近事件',

        // Syscall Panel
        'syscall.distribution': '系统调用分布',
        'syscall.rate': '调用速率',
        'syscall.topSyscalls': '热门系统调用',
        'syscall.fileIO': '文件I/O统计',
        'syscall.recentSyscalls': '最近调用',

        // Performance Panel
        'perf.ctxSwitches': '上下文切换',
        'perf.monitoredFuncs': '监控函数数',
        'perf.ctxSwitchRate': '切换速率',
        'perf.funcCallTime': '函数调用耗时',
        'perf.funcStats': '函数统计 (uprobe)',
        'perf.recentFuncCalls': '最近函数调用',

        // Table Headers
        'table.process': '进程',
        'table.source': '来源',
        'table.dest': '目标',
        'table.sent': '发送',
        'table.recv': '接收',
        'table.duration': '持续时间',
        'table.syscall': '系统调用',
        'table.count': '次数',
        'table.avgTime': '平均耗时(us)',
        'table.errors': '错误数',
        'table.errorRate': '错误率',
        'table.file': '文件',
        'table.read': '读取',
        'table.write': '写入',
        'table.rCount': '读次数',
        'table.wCount': '写次数',
        'table.function': '函数',
        'table.calls': '调用数',
        'table.total': '总耗时(ms)',
        'table.avg': '平均(us)',
        'table.min': '最小(us)',
        'table.max': '最大(us)',

        // Chart Labels
        'chart.sent': '发送 (KB/s)',
        'chart.recv': '接收 (KB/s)',
        'chart.activeConn': '活跃连接',
        'chart.syscallsPerSec': '调用/秒',
        'chart.switchesPerSec': '切换/秒',
        'chart.avgTimeUs': '平均耗时(us)'
    }
};

// 当前语言
let currentLang = localStorage.getItem('ebpf-debugger-lang') || 'en';

/**
 * 获取翻译文本
 */
function t(key) {
    return i18nData[currentLang][key] || i18nData['en'][key] || key;
}

/**
 * 切换语言
 */
function toggleLanguage() {
    currentLang = currentLang === 'en' ? 'zh' : 'en';
    localStorage.setItem('ebpf-debugger-lang', currentLang);
    applyLanguage();
    updateLangButton();

    // 更新图表标签
    if (typeof updateChartLabels === 'function') {
        updateChartLabels();
    }
}

/**
 * 更新语言切换按钮文本
 */
function updateLangButton() {
    const btn = document.getElementById('lang-btn');
    if (btn) {
        btn.textContent = currentLang === 'en' ? '中文' : 'English';
    }
}

/**
 * 应用语言到所有带有data-i18n属性的元素
 */
function applyLanguage() {
    document.querySelectorAll('[data-i18n]').forEach(el => {
        const key = el.getAttribute('data-i18n');
        const text = t(key);
        if (text) {
            el.textContent = text;
        }
    });

    // 更新页面标题
    document.documentElement.lang = currentLang === 'zh' ? 'zh-CN' : 'en';
}

/**
 * 获取当前语言
 */
function getCurrentLang() {
    return currentLang;
}

// 页面加载时应用语言
document.addEventListener('DOMContentLoaded', () => {
    applyLanguage();
    updateLangButton();
});
