#!/usr/bin/env bash
# 从 firmware/epw.env 生成两个工程的 sdkconfig.local（均不入库）。
#
# 用法：
#   ./sync_env.sh            # 读取 firmware/epw.env
#   ./sync_env.sh other.env  # 读取指定文件

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FW_DIR="$(cd "$HERE/../.." && pwd)"
ENV_FILE="${1:-$FW_DIR/epw.env}"

if [[ ! -f "$ENV_FILE" ]]; then
  echo "缺少 $ENV_FILE" >&2
  echo "先复制模板：cp '$FW_DIR/epw.env.example' '$ENV_FILE' 并填写。" >&2
  exit 1
fi

set -a
# shellcheck disable=SC1090
. "$ENV_FILE"
set +a

quote() {
  printf '%s' "${1-}" | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g'
}

emit() {
  printf 'CONFIG_%s="%s"\n' "$1" "$(quote "${2-}")"
}

ui_cfg="$FW_DIR/ui_demo/sdkconfig.local"
{
  echo "# 由 tools/sync_env.sh 从 $(basename "$ENV_FILE") 生成，请勿提交。"
  emit EPW_WIFI_SSID "${EPW_WIFI_SSID-}"
  emit EPW_WIFI_PASSWORD "${EPW_WIFI_PASSWORD-}"
  emit EPW_PET_BRIDGE_URL "${EPW_PET_BRIDGE_URL-}"
  emit EPW_VOICE_WS_URL "${EPW_VOICE_WS_URL-}"
  emit EPW_VOICE_TOKEN "${EPW_VOICE_TOKEN-}"
  emit EPW_NURSERY_BASE "${EPW_NURSERY_BASE-}"
} > "$ui_cfg"

main_cfg="$FW_DIR/sdkconfig.local"
{
  echo "# 由 tools/sync_env.sh 从 $(basename "$ENV_FILE") 生成，请勿提交。"
  emit EPW_WIFI_SSID "${EPW_WIFI_SSID-}"
  emit EPW_WIFI_PASSWORD "${EPW_WIFI_PASSWORD-}"
  emit EPW_OTA_URL "${EPW_OTA_URL-}"
} > "$main_cfg"

echo "已生成："
echo "  $ui_cfg"
echo "  $main_cfg"
echo
echo "构建 ui_demo 时使用："
echo "  cd '$FW_DIR/ui_demo' && SDKCONFIG_DEFAULTS=\"sdkconfig.defaults;sdkconfig.local\" idf.py build"
