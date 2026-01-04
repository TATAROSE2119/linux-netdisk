/**
 * Realtime Module - WebSocket communication and UI updates
 */

let socket = null;
let isRunning = false;

/**
 * 格式化字节数
 */
function formatBytes(bytes) {
    if (bytes === 0) return '0 B';
    const k = 1024;
    const sizes = ['B', 'KB', 'MB', 'GB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
}

/**
 * 格式化时间
 */
function formatTime(seconds) {
    if (seconds < 60) return seconds.toFixed(1) + 's';
    const mins = Math.floor(seconds / 60);
    const secs = (seconds % 60).toFixed(0);
    return `${mins}m ${secs}s`;
}

/**
 * 格式化时间戳
 */
function formatTimestamp(ts) {
    const date = new Date(ts * 1000);
    return date.toLocaleTimeString();
}

/**
 * 初始化WebSocket连接
 */
function initSocket() {
    socket = io();

    socket.on('connect', () => {
        console.log('Connected to server');
        updateStatus('connected', 'status.connected');
    });

    socket.on('disconnect', () => {
        console.log('Disconnected from server');
        updateStatus('disconnected', 'status.disconnected');
    });

    socket.on('update', (data) => {
        updateDashboard(data);
    });
}

/**
 * 更新状态显示
 */
function updateStatus(status, textKey) {
    const dot = document.getElementById('status-indicator');
    const textEl = document.getElementById('status-text');
    const btn = document.getElementById('toggle-btn');

    dot.className = 'status-dot';

    // 使用i18n获取文本
    const text = typeof t === 'function' ? t(textKey) : textKey;

    if (status === 'connected') {
        dot.classList.add('connected');
    } else if (status === 'running') {
        dot.classList.add('running');
        btn.textContent = typeof t === 'function' ? t('btn.stop') : 'Stop';
        btn.classList.add('stop');
        isRunning = true;
    } else if (status === 'stopped') {
        dot.classList.add('connected');
        btn.textContent = typeof t === 'function' ? t('btn.start') : 'Start';
        btn.classList.remove('stop');
        isRunning = false;
    }

    textEl.textContent = text;
}

/**
 * 更新整个仪表板
 */
function updateDashboard(data) {
    if (data.network) {
        updateNetworkPanel(data.network);
        updateNetworkCharts(data.network);
    }
    if (data.syscall) {
        updateSyscallPanel(data.syscall);
        updateSyscallCharts(data.syscall);
    }
    if (data.perf) {
        updatePerfPanel(data.perf);
        updatePerfCharts(data.perf);
    }

    updateStatus('running', 'status.running');
}

/**
 * 更新网络面板
 */
function updateNetworkPanel(data) {
    // 更新统计卡片
    document.getElementById('active-connections').textContent = data.active_connections || 0;
    document.getElementById('total-connections').textContent = data.total_connections || 0;
    document.getElementById('total-sent').textContent = formatBytes(data.total_bytes_sent || 0);
    document.getElementById('total-recv').textContent = formatBytes(data.total_bytes_recv || 0);

    // 更新连接表格
    const tbody = document.querySelector('#connections-table tbody');
    tbody.innerHTML = '';

    if (data.connections) {
        data.connections.forEach(conn => {
            const tr = document.createElement('tr');
            tr.innerHTML = `
                <td>${conn.pid}</td>
                <td>${conn.comm}</td>
                <td>${conn.src}</td>
                <td>${conn.dst}</td>
                <td>${formatBytes(conn.bytes_sent)}</td>
                <td>${formatBytes(conn.bytes_recv)}</td>
                <td>${formatTime(conn.duration)}</td>
            `;
            tbody.appendChild(tr);
        });
    }

    // 更新事件列表
    const eventsList = document.getElementById('network-events');
    eventsList.innerHTML = '';

    if (data.recent_events) {
        data.recent_events.slice().reverse().forEach(event => {
            const div = document.createElement('div');
            div.className = 'event-item';
            div.innerHTML = `
                <span class="event-time">${formatTimestamp(event.time)}</span>
                <span class="event-type ${event.type}">${event.type}</span>
                <span>${event.comm} (${event.pid}): ${event.src} → ${event.dst}</span>
            `;
            eventsList.appendChild(div);
        });
    }
}

/**
 * 更新系统调用面板
 */
function updateSyscallPanel(data) {
    // 更新系统调用表格
    const tbody = document.querySelector('#syscalls-table tbody');
    tbody.innerHTML = '';

    if (data.syscalls) {
        data.syscalls.forEach(syscall => {
            const tr = document.createElement('tr');
            tr.innerHTML = `
                <td>${syscall.name} <small>(${syscall.name_cn})</small></td>
                <td>${syscall.count}</td>
                <td>${syscall.avg_time_us.toFixed(2)}</td>
                <td>${syscall.errors}</td>
                <td>${syscall.error_rate.toFixed(1)}%</td>
            `;
            tbody.appendChild(tr);
        });
    }

    // 更新文件I/O表格
    const fileIoTbody = document.querySelector('#file-io-table tbody');
    fileIoTbody.innerHTML = '';

    if (data.file_io) {
        data.file_io.forEach(file => {
            const tr = document.createElement('tr');
            tr.innerHTML = `
                <td title="${file.filename}">${file.filename.slice(-40)}</td>
                <td>${formatBytes(file.read_bytes)}</td>
                <td>${formatBytes(file.write_bytes)}</td>
                <td>${file.read_count}</td>
                <td>${file.write_count}</td>
            `;
            fileIoTbody.appendChild(tr);
        });
    }

    // 更新事件列表
    const eventsList = document.getElementById('syscall-events');
    eventsList.innerHTML = '';

    if (data.recent_events) {
        data.recent_events.slice().reverse().forEach(event => {
            const div = document.createElement('div');
            div.className = 'event-item';
            const status = event.ret < 0 ? '❌' : '✓';
            div.innerHTML = `
                <span class="event-time">${formatTimestamp(event.time)}</span>
                <span class="event-type syscall">${event.syscall}</span>
                <span>${event.comm} (${event.pid}): ${event.duration_us.toFixed(0)}us ${status}</span>
            `;
            eventsList.appendChild(div);
        });
    }
}

/**
 * 更新性能面板
 */
function updatePerfPanel(data) {
    // 更新统计卡片
    document.getElementById('context-switches').textContent = data.total_context_switches || 0;
    document.getElementById('monitored-funcs').textContent = data.functions ? data.functions.length : 0;

    // 更新函数表格
    const tbody = document.querySelector('#functions-table tbody');
    tbody.innerHTML = '';

    if (data.functions) {
        data.functions.forEach(func => {
            const tr = document.createElement('tr');
            tr.innerHTML = `
                <td>${func.name}</td>
                <td>${func.call_count}</td>
                <td>${func.total_time_ms.toFixed(2)}</td>
                <td>${func.avg_time_us.toFixed(2)}</td>
                <td>${func.min_time_us.toFixed(2)}</td>
                <td>${func.max_time_us.toFixed(2)}</td>
            `;
            tbody.appendChild(tr);
        });
    }

    // 更新事件列表
    const eventsList = document.getElementById('perf-events');
    eventsList.innerHTML = '';

    if (data.recent_events) {
        data.recent_events.slice().reverse().forEach(event => {
            const div = document.createElement('div');
            div.className = 'event-item';
            const typeLabel = event.type === 'func_return' ? 'return' : 'entry';
            const duration = event.duration_us ? ` (${event.duration_us.toFixed(0)}us)` : '';
            div.innerHTML = `
                <span class="event-time">${formatTimestamp(event.time)}</span>
                <span class="event-type func">${typeLabel}</span>
                <span>${event.func}${duration} - ${event.comm} (${event.pid})</span>
            `;
            eventsList.appendChild(div);
        });
    }
}

/**
 * 切换标签页
 */
function switchTab(tabName) {
    // 更新按钮状态
    document.querySelectorAll('.tab-btn').forEach(btn => {
        btn.classList.toggle('active', btn.dataset.tab === tabName);
    });

    // 更新面板显示
    document.querySelectorAll('.panel').forEach(panel => {
        panel.classList.toggle('active', panel.id === `${tabName}-panel`);
    });
}

/**
 * 切换监控状态
 */
function toggleMonitoring() {
    if (isRunning) {
        socket.emit('stop_monitoring');
        updateStatus('stopped', 'status.stopped');
    } else {
        socket.emit('start_monitoring');
        updateStatus('running', 'status.starting');
    }
}

// 页面加载时初始化
document.addEventListener('DOMContentLoaded', () => {
    initSocket();

    // 标签切换事件
    document.querySelectorAll('.tab-btn').forEach(btn => {
        btn.addEventListener('click', () => switchTab(btn.dataset.tab));
    });

    // 监控开关按钮
    document.getElementById('toggle-btn').addEventListener('click', toggleMonitoring);
});
