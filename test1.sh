#!/bin/bash

USERNUM=50
FAILED=0

# 2. 校验本地原文件与下载文件是否一致
for i in $(seq 1 $USERNUM); do
    if diff file${i}.bin netdisk_data/user$i/file${i}.bin >/dev/null 2>&1; then
        echo "✅ 用户 testuser$i 文件 file${i}.bin 校验一致"
    else
        echo "❌ 用户 testuser$i 文件 file${i}.bin 校验失败！"
        FAILED=1
    fi
done

if [ $FAILED -eq 0 ]; then
    echo "所有用户上传/下载文件内容完全一致！"
else
    echo "存在文件内容不一致，请检查代码或网络传输！"
fi
