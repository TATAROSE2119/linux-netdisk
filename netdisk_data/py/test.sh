#!/bin/bash

for i in {1..50}; do
    cp file1.bin file${i}.bin   # 假设你只有一个大文件，复制为5份
    expect test_concurrent.exp $i &
done

wait
echo "所有expect并发测试完成"
