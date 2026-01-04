"""API routes for eBPF debugger"""

from flask import Blueprint, jsonify

api_bp = Blueprint('api', __name__, url_prefix='/api')

# 这些将在app.py中设置
collectors = {
    'network': None,
    'syscall': None,
    'perf': None
}


def set_collectors(network, syscall, perf):
    """设置收集器引用"""
    collectors['network'] = network
    collectors['syscall'] = syscall
    collectors['perf'] = perf


@api_bp.route('/health')
def health():
    """健康检查"""
    return jsonify({
        'status': 'ok',
        'collectors': {
            'network': collectors['network'] is not None,
            'syscall': collectors['syscall'] is not None,
            'perf': collectors['perf'] is not None
        }
    })


@api_bp.route('/network')
def get_network_stats():
    """获取网络监控数据"""
    if collectors['network'] is None:
        return jsonify({'error': 'Network collector not initialized'}), 503
    return jsonify(collectors['network'].get_stats())


@api_bp.route('/syscall')
def get_syscall_stats():
    """获取系统调用数据"""
    if collectors['syscall'] is None:
        return jsonify({'error': 'Syscall collector not initialized'}), 503
    return jsonify(collectors['syscall'].get_stats())


@api_bp.route('/perf')
def get_perf_stats():
    """获取性能数据"""
    if collectors['perf'] is None:
        return jsonify({'error': 'Perf collector not initialized'}), 503
    return jsonify(collectors['perf'].get_stats())


@api_bp.route('/all')
def get_all_stats():
    """获取所有数据"""
    result = {}

    if collectors['network']:
        result['network'] = collectors['network'].get_stats()

    if collectors['syscall']:
        result['syscall'] = collectors['syscall'].get_stats()

    if collectors['perf']:
        result['perf'] = collectors['perf'].get_stats()

    return jsonify(result)
