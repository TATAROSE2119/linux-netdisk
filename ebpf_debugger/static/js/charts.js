/**
 * Charts Module - Chart.js configurations for eBPF Debugger
 */

// 图表配置
const chartColors = {
    sent: 'rgba(233, 69, 96, 0.8)',
    recv: 'rgba(78, 204, 163, 0.8)',
    primary: 'rgba(233, 69, 96, 0.8)',
    secondary: 'rgba(78, 204, 163, 0.8)',
    grid: 'rgba(255, 255, 255, 0.1)',
    text: '#aaa'
};

const defaultOptions = {
    responsive: true,
    maintainAspectRatio: false,
    plugins: {
        legend: {
            labels: {
                color: chartColors.text
            }
        }
    },
    scales: {
        x: {
            grid: {
                color: chartColors.grid
            },
            ticks: {
                color: chartColors.text
            }
        },
        y: {
            grid: {
                color: chartColors.grid
            },
            ticks: {
                color: chartColors.text
            }
        }
    }
};

// 图表实例
let charts = {};

/**
 * 获取图表标签(支持i18n)
 */
function getChartLabel(key) {
    return typeof t === 'function' ? t(key) : key;
}

/**
 * 初始化所有图表
 */
function initCharts() {
    // 吞吐量图表
    const throughputCtx = document.getElementById('throughput-chart');
    if (throughputCtx) {
        charts.throughput = new Chart(throughputCtx, {
            type: 'line',
            data: {
                labels: [],
                datasets: [
                    {
                        label: getChartLabel('chart.sent'),
                        data: [],
                        borderColor: chartColors.sent,
                        backgroundColor: 'rgba(233, 69, 96, 0.1)',
                        tension: 0.4,
                        fill: true
                    },
                    {
                        label: getChartLabel('chart.recv'),
                        data: [],
                        borderColor: chartColors.recv,
                        backgroundColor: 'rgba(78, 204, 163, 0.1)',
                        tension: 0.4,
                        fill: true
                    }
                ]
            },
            options: defaultOptions
        });
    }

    // 连接数图表
    const connectionsCtx = document.getElementById('connections-chart');
    if (connectionsCtx) {
        charts.connections = new Chart(connectionsCtx, {
            type: 'line',
            data: {
                labels: [],
                datasets: [{
                    label: getChartLabel('chart.activeConn'),
                    data: [],
                    borderColor: chartColors.primary,
                    backgroundColor: 'rgba(233, 69, 96, 0.1)',
                    tension: 0.4,
                    fill: true
                }]
            },
            options: defaultOptions
        });
    }

    // 系统调用分布饼图
    const syscallPieCtx = document.getElementById('syscall-pie-chart');
    if (syscallPieCtx) {
        charts.syscallPie = new Chart(syscallPieCtx, {
            type: 'doughnut',
            data: {
                labels: [],
                datasets: [{
                    data: [],
                    backgroundColor: [
                        '#e94560', '#4ecca3', '#3498db', '#9b59b6',
                        '#e67e22', '#1abc9c', '#f39c12', '#2ecc71'
                    ]
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                plugins: {
                    legend: {
                        position: 'right',
                        labels: {
                            color: chartColors.text
                        }
                    }
                }
            }
        });
    }

    // 系统调用速率图表
    const syscallRateCtx = document.getElementById('syscall-rate-chart');
    if (syscallRateCtx) {
        charts.syscallRate = new Chart(syscallRateCtx, {
            type: 'line',
            data: {
                labels: [],
                datasets: [{
                    label: getChartLabel('chart.syscallsPerSec'),
                    data: [],
                    borderColor: chartColors.primary,
                    backgroundColor: 'rgba(233, 69, 96, 0.1)',
                    tension: 0.4,
                    fill: true
                }]
            },
            options: defaultOptions
        });
    }

    // 上下文切换图表
    const contextSwitchCtx = document.getElementById('context-switch-chart');
    if (contextSwitchCtx) {
        charts.contextSwitch = new Chart(contextSwitchCtx, {
            type: 'line',
            data: {
                labels: [],
                datasets: [{
                    label: getChartLabel('chart.switchesPerSec'),
                    data: [],
                    borderColor: chartColors.secondary,
                    backgroundColor: 'rgba(78, 204, 163, 0.1)',
                    tension: 0.4,
                    fill: true
                }]
            },
            options: defaultOptions
        });
    }

    // 函数调用时间图表
    const funcTimeCtx = document.getElementById('func-time-chart');
    if (funcTimeCtx) {
        charts.funcTime = new Chart(funcTimeCtx, {
            type: 'bar',
            data: {
                labels: [],
                datasets: [{
                    label: getChartLabel('chart.avgTimeUs'),
                    data: [],
                    backgroundColor: chartColors.primary
                }]
            },
            options: {
                ...defaultOptions,
                indexAxis: 'y'
            }
        });
    }
}

/**
 * 更新图表标签(语言切换时调用)
 */
function updateChartLabels() {
    if (charts.throughput) {
        charts.throughput.data.datasets[0].label = getChartLabel('chart.sent');
        charts.throughput.data.datasets[1].label = getChartLabel('chart.recv');
        charts.throughput.update('none');
    }
    if (charts.connections) {
        charts.connections.data.datasets[0].label = getChartLabel('chart.activeConn');
        charts.connections.update('none');
    }
    if (charts.syscallRate) {
        charts.syscallRate.data.datasets[0].label = getChartLabel('chart.syscallsPerSec');
        charts.syscallRate.update('none');
    }
    if (charts.contextSwitch) {
        charts.contextSwitch.data.datasets[0].label = getChartLabel('chart.switchesPerSec');
        charts.contextSwitch.update('none');
    }
    if (charts.funcTime) {
        charts.funcTime.data.datasets[0].label = getChartLabel('chart.avgTimeUs');
        charts.funcTime.update('none');
    }
}

/**
 * 更新网络图表
 */
function updateNetworkCharts(data) {
    if (!data) return;

    // 更新吞吐量图表
    if (charts.throughput && data.throughput_history) {
        const history = data.throughput_history;
        const labels = history.map((_, i) => i);

        // 计算每秒吞吐量
        const sentRates = [];
        const recvRates = [];
        for (let i = 1; i < history.length; i++) {
            const sentDelta = (history[i].sent - history[i-1].sent) / 1024;
            const recvDelta = (history[i].recv - history[i-1].recv) / 1024;
            sentRates.push(Math.max(0, sentDelta));
            recvRates.push(Math.max(0, recvDelta));
        }

        charts.throughput.data.labels = labels.slice(1);
        charts.throughput.data.datasets[0].data = sentRates;
        charts.throughput.data.datasets[1].data = recvRates;
        charts.throughput.update('none');
    }

    // 更新连接数图表
    if (charts.connections && data.connection_history) {
        const history = data.connection_history;
        const labels = history.map((_, i) => i);
        const counts = history.map(h => h.count);

        charts.connections.data.labels = labels;
        charts.connections.data.datasets[0].data = counts;
        charts.connections.update('none');
    }
}

/**
 * 更新系统调用图表
 */
function updateSyscallCharts(data) {
    if (!data) return;

    // 更新饼图
    if (charts.syscallPie && data.syscalls) {
        const topSyscalls = data.syscalls.slice(0, 8);
        charts.syscallPie.data.labels = topSyscalls.map(s => s.name);
        charts.syscallPie.data.datasets[0].data = topSyscalls.map(s => s.count);
        charts.syscallPie.update('none');
    }

    // 更新速率图表
    if (charts.syscallRate && data.syscall_rate_history) {
        const history = data.syscall_rate_history;
        const labels = history.map((_, i) => i);
        const rates = history.map(h => h.rate);

        charts.syscallRate.data.labels = labels;
        charts.syscallRate.data.datasets[0].data = rates;
        charts.syscallRate.update('none');
    }
}

/**
 * 更新性能图表
 */
function updatePerfCharts(data) {
    if (!data) return;

    // 更新上下文切换图表
    if (charts.contextSwitch && data.context_switch_history) {
        const history = data.context_switch_history;
        const labels = history.map((_, i) => i);
        const rates = history.map(h => h.rate);

        charts.contextSwitch.data.labels = labels;
        charts.contextSwitch.data.datasets[0].data = rates;
        charts.contextSwitch.update('none');
    }

    // 更新函数时间图表
    if (charts.funcTime && data.functions) {
        const funcs = data.functions.slice(0, 10);
        charts.funcTime.data.labels = funcs.map(f => f.name);
        charts.funcTime.data.datasets[0].data = funcs.map(f => f.avg_time_us);
        charts.funcTime.update('none');
    }
}

// 页面加载时初始化图表
document.addEventListener('DOMContentLoaded', initCharts);
