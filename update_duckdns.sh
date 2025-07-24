#!/bin/bash
# 更新DuckDNS IP地址脚本

DOMAIN="tatapan"
TOKEN="d31c8e89-fa0b-4339-8cc4-738993cf2159"

echo "🌐 更新DuckDNS域名IP地址..."

# 获取当前公网IP
CURRENT_IP=$(curl -s ifconfig.me)
echo "当前公网IP: $CURRENT_IP"

# 更新DuckDNS
RESPONSE=$(curl -s "https://www.duckdns.org/update?domains=${DOMAIN}&token=${TOKEN}&ip=${CURRENT_IP}")

if [ "$RESPONSE" = "OK" ]; then
    echo "✅ DuckDNS更新成功!"
    echo "域名: tatapan.duckdns.org"
    echo "指向IP: $CURRENT_IP"
else
    echo "❌ DuckDNS更新失败: $RESPONSE"
fi