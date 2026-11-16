#!/usr/bin/env bash
# 在电脑上模拟设备请求 OTA 端点，板子还没到也能验证服务端固件仓库。
#
# 用法：
#   ./ota_probe.sh
#   ./ota_probe.sh http://124.222.233.196:5801/xiaozhi/ota/ 00:11:22:33:44:55 0.1.0 epw-watch-175

set -euo pipefail

URL="${1:-http://124.222.233.196:5801/xiaozhi/ota/}"
DEVICE_ID="${2:-00:11:22:33:44:55}"
VERSION="${3:-0.1.0}"
MODEL="${4:-epw-watch-175}"

BODY=$(printf '{"application":{"name":"english-pet-watch","version":"%s"},"board":{"type":"%s","name":"English Pet Watch"},"uuid":"%s"}' \
  "$VERSION" "$MODEL" "$DEVICE_ID")

echo "POST $URL"
echo "device-model: $MODEL  device-version: $VERSION"
echo

curl -sS --max-time 20 -X POST "$URL" \
  -H "Content-Type: application/json" \
  -H "device-id: $DEVICE_ID" \
  -H "client-id: $DEVICE_ID" \
  -H "device-model: $MODEL" \
  -H "device-version: $VERSION" \
  -d "$BODY"
echo
