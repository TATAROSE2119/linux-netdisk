from flask import Flask, request, jsonify,send_from_directory,render_template
import os
import sqlite3
import hashlib
import os.path # For absolute paths

app= Flask(__name__)
app.config['MAX_CONTENT_LENGTH'] = 10 * 1024 * 1024 * 1024  # 最大上传大小: 10GB
UPLOAD_FOLDER = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'netdisk_data')
DB_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'netdisk.db')
os.makedirs(UPLOAD_FOLDER, exist_ok=True) # 确保上传目录存在 如果已经存在就忽略错误

#-----工具函数部分-----
def hash_password(password):
    """使用SHA-256对密码进行哈希"""
    return hashlib.sha256(password.encode('utf-8')).hexdigest()
def get_user_dir(username):
    """获取用户的上传目录"""
    return os.path.join(UPLOAD_FOLDER, username)
def init_db():
    """初始化数据库"""
    conn = sqlite3.connect(DB_FILE)
    c= conn.cursor()
    c.execute('''CREATE TABLE IF NOT EXISTS users (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        username TEXT UNIQUE,
        password TEXT 
        )''')
    conn.commit()
    conn.close()

init_db()  # 确保数据库和表存在

#-----路由部分-----
@app.route('/')
def index():
    """主页"""
    return render_template('index.html')
@app.route('/api/register', methods=['POST'])
def register():
    """用户注册"""
    data = request.json
    username = data.get('username')
    password = data.get('password')
    
    if not username or not password:
        return jsonify({'ok':False,'msg': '用户名和密码不能为空'})
    password_hash = hash_password(password)
    conn= sqlite3.connect(DB_FILE)
    c = conn.cursor() # 获取游标
    try:
        c.execute('INSERT INTO users (username, password) VALUES (?, ?)', (username, password_hash))
        conn.commit()
        # 创建用户目录
        user_dir = get_user_dir(username)
        os.makedirs(user_dir, exist_ok=True)
        return jsonify({'ok': True, 'message': '注册成功✅'})
    except sqlite3.IntegrityError:
        return jsonify({'ok': False, 'msg': '用户名已存在❌'})
    finally:
        conn.close()
@app.route('/api/login', methods=['POST'])
def login():
    """用户登录"""
    try:
    data = request.json # 获取JSON数据
    username = data.get('username')
    password = data.get('password')
    if not username or not password:
        return jsonify({'ok': False, 'msg': '用户名和密码不能为空'})
    password_hash = hash_password(password)
    conn = sqlite3.connect(DB_FILE)
    c = conn.cursor() # 获取游标
    c.execute('SELECT * FROM users WHERE username = ? AND password = ?', (username, password_hash))
    row = c.fetchone() # 获取查询结果
    conn.close()
    if row is None:
            return jsonify({'ok': False, 'msg': '用户名或密码错误❌'})
        return jsonify({'ok': True, 'msg': '登录成功✅'})
    except Exception as e:
        return jsonify({'ok': False, 'msg': f'登录失败: {str(e)}'})
@app.route('/api/upload', methods=['POST'])
def upload_file():
    """文件上传"""
    try:
    username = request.form.get('username')
    if not username:
            return jsonify({'ok': False, 'msg': '未登录'}), 401
        file = request.files.get('file')
    if not file:
            return jsonify({'ok': False, 'msg': '未选择文件'}), 400
    user_dir = get_user_dir(username)
    os.makedirs(user_dir, exist_ok=True)
        filepath = os.path.join(user_dir, file.filename)
        file.save(filepath)
    return jsonify({'ok': True, 'message': '文件上传成功✅'})
    except Exception as e:
        return jsonify({'ok': False, 'msg': f'上传失败: {str(e)}'}), 500
@app.route('/api/upload_chunk', methods=['POST'])
def upload_chunk():
    """上传文件分片"""
    try:
        username = request.form.get('username')
        if not username:
            return jsonify({'ok': False, 'msg': '未登录'}), 401
        filename = request.form.get('filename')
        chunk_index = int(request.form.get('chunkIndex'))
        total_chunks = int(request.form.get('totalChunks'))
        file = request.files.get('file')
        if not file or not filename:
            return jsonify({'ok': False, 'msg': '缺少文件或参数'}), 400
        user_dir = get_user_dir(username)
        tmp_dir = os.path.join(user_dir, '.tmp')
        os.makedirs(tmp_dir, exist_ok=True)
        chunk_path = os.path.join(tmp_dir, f'{filename}_{chunk_index}')
        file.save(chunk_path)
        return jsonify({'ok': True, 'msg': '分片上传成功'})
    except Exception as e:
        return jsonify({'ok': False, 'msg': f'分片上传失败: {str(e)}'}), 500

@app.route('/api/merge_chunks', methods=['POST'])
def merge_chunks():
    """合并文件分片"""
    try:
        data = request.json
        username = data.get('username')
        filename = data.get('filename')
        total_chunks = data.get('totalChunks')
        if not username or not filename or not total_chunks:
            return jsonify({'ok': False, 'msg': '缺少参数'}), 400
        user_dir = get_user_dir(username)
        tmp_dir = os.path.join(user_dir, '.tmp')
        final_path = os.path.join(user_dir, filename)
        with open(final_path, 'wb') as outfile:
            for i in range(total_chunks):
                chunk_path = os.path.join(tmp_dir, f'{filename}_{i}')
                if not os.path.exists(chunk_path):
                    return jsonify({'ok': False, 'msg': f'分片 {i} 缺失'}), 400
                with open(chunk_path, 'rb') as infile:
                    outfile.write(infile.read())
                os.remove(chunk_path)
        os.rmdir(tmp_dir) if os.path.exists(tmp_dir) and not os.listdir(tmp_dir) else None
        return jsonify({'ok': True, 'msg': '文件合并成功✅'})
    except Exception as e:
        return jsonify({'ok': False, 'msg': f'合并失败: {str(e)}'}), 500

@app.route('/api/list', methods=['GET'])
def list_files():
    """列出用户文件"""
    username = request.args.get('username')
    userdir= get_user_dir(username)
    if not os.path.exists(userdir):
        return jsonify({'files': []})
    files=[f for f in os.listdir(userdir) if not f.startswith('.')]
    return jsonify({'files': files})
@app.route('/api/download', methods=['GET'])
def download_file():
    """文件下载"""
    try:
    username = request.args.get('username')
    filename = request.args.get('filename')
        if not username or not filename:
            return jsonify({'ok': False, 'msg': '缺少参数'}), 400
    user_dir = get_user_dir(username)
        filepath = os.path.join(user_dir, filename)
        if not os.path.exists(filepath):
            return jsonify({'ok': False, 'msg': f'文件不存在: {filepath}'}), 404
    return send_from_directory(user_dir, filename, as_attachment=True)
    except Exception as e:
        return jsonify({'ok': False, 'msg': f'下载失败: {str(e)} - Path: {filepath}'}), 500

@app.route('/api/rename', methods=['POST'])
def rename_file():
    """文件改名"""
    try:
        data = request.json
        username = data.get('username')
        old_filename = data.get('old_filename')
        new_filename = data.get('new_filename')
        if not username or not old_filename or not new_filename:
            return jsonify({'ok': False, 'msg': '缺少参数'})
        user_dir = get_user_dir(username)
        old_path = os.path.join(user_dir, old_filename)
        new_path = os.path.join(user_dir, new_filename)
        if not os.path.exists(old_path):
            return jsonify({'ok': False, 'msg': '原文件不存在❌'})
        if os.path.exists(new_path):
            return jsonify({'ok': False, 'msg': '新文件名已存在❌'})
        os.rename(old_path, new_path)
        return jsonify({'ok': True, 'msg': '改名成功✅'})
    except Exception as e:
        return jsonify({'ok': False, 'msg': f'改名失败: {str(e)}'})

@app.route('/api/delete', methods=['POST'])
def delete_file():
    """文件删除"""
    data = request.json
    username = data.get('username')
    filename = data.get('filename')
    if not username or not filename:
        return jsonify({'ok': False, 'msg': '缺少参数'})
    filepath = os.path.join(get_user_dir(username), filename)
    if os.path.exists(filepath):
        os.remove(filepath)
        return jsonify({'ok': True, 'msg': '删除成功✅'})
    else:
        return jsonify({'ok': False, 'msg': '文件不存在❌'})

if __name__ == '__main__':
    app.run(debug=True)


