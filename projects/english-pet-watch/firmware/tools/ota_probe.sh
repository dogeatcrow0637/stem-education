#!/usr/bin/env bash
# 在电脑上模拟设备请求 OTA 端点，板子还没到也能验证服务端固件仓库。
#
# 用法：
#   ./ota_probe.sh
#   ./ota_probe.sh http://<服务器IP>:5801/xiaozhi/ota/ 00:11:22:33:44:55 0.1.0 epw-watch-175
#
# 未传 URL 时读取 firmware/epw.env 的 EPW_OTA_URL。

set -euo pipefail

ENV_FILE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/epw.env"
if [[ -f "$ENV_FILE" ]]; then
  set -a
  # shellcheck disable=SC1090
  . "$ENV_FILE"
  set +a
fi

URL="${1:-${EPW_OTA_URL:-}}"
if [[ -z "$URL" ]]; then
  echo "缺少 OTA 地址：传参或在 firmware/epw.env 配置 EPW_OTA_URL。" >&2
  exit 1
fi

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
