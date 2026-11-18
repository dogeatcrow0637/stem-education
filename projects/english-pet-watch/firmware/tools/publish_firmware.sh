#!/usr/bin/env bash
# 把编译产物发布到服务器上的 xiaozhi-server 固件目录。
# 服务端只认 data/bin/{model}_{version}.bin 这种文件名。
#
# 用法：
#   ./publish_firmware.sh build/english_pet_watch.bin 0.2.0
#   ./publish_firmware.sh build/english_pet_watch.bin 0.2.0 epw-watch-175
#
# 主机与远端目录读取 firmware/epw.env：
#   EPW_SERVER_HOST     默认 vls
#   EPW_SERVER_BIN_DIR  必填

set -euo pipefail

ENV_FILE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/epw.env"
if [[ -f "$ENV_FILE" ]]; then
  set -a
  # shellcheck disable=SC1090
  . "$ENV_FILE"
  set +a
fi

BIN="${1:-}"
VERSION="${2:-}"
MODEL="${3:-epw-watch-175}"
SERVER="${EPW_SERVER_HOST:-vls}"
REMOTE_DIR="${EPW_SERVER_BIN_DIR:-}"

if [[ -z "$REMOTE_DIR" ]]; then
  echo "缺少 EPW_SERVER_BIN_DIR：请在 firmware/epw.env 配置。" >&2
  exit 1
fi

if [[ -z "$BIN" || -z "$VERSION" ]]; then
  echo "用法: $0 <firmware.bin> <version> [model]" >&2
  echo "示例: $0 build/english_pet_watch.bin 0.2.0" >&2
  exit 1
fi

if [[ ! -f "$BIN" ]]; then
  echo "找不到固件文件: $BIN" >&2
  exit 1
fi

if [[ ! "$VERSION" =~ ^[0-9][A-Za-z0-9._-]*$ ]]; then
  echo "版本号不合法: $VERSION（必须数字开头，只含字母/数字/点/下划线/短横线）" >&2
  exit 1
fi

if [[ ! "$MODEL" =~ ^[A-Za-z0-9._-]+$ ]]; then
  echo "型号不合法: $MODEL" >&2
  exit 1
fi

TARGET="${MODEL}_${VERSION}.bin"

echo "发布: $BIN -> ${SERVER}:${REMOTE_DIR}/${TARGET}"
ssh "$SERVER" "mkdir -p '$REMOTE_DIR'"
scp "$BIN" "${SERVER}:${REMOTE_DIR}/${TARGET}"

echo "服务端文件列表:"
ssh "$SERVER" "ls -lh '$REMOTE_DIR'"

echo
echo "完成。设备下次请求 OTA 时会看到 ${VERSION}。"
echo "手动验证:"
echo "  ./ota_probe.sh http://<服务器地址>:5801/xiaozhi/ota/ <device-id> <当前版本> ${MODEL}"
