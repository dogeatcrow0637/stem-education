#!/usr/bin/env bash
# 用途：检查 Sesame Robot 开发机上的基础工具是否存在（只读，不安装）
# 用法：bash projects/sesame-robot/software/tools/check_env.sh
# 依赖：bash；可选 arduino-cli
# 影响范围：无，仅打印检查结果
set -euo pipefail

check() {
  local name="$1" cmd="$2" hint="$3"
  if command -v "${cmd}" >/dev/null 2>&1; then
    printf '[OK]   %-12s %s\n' "${name}" "$(command -v "${cmd}")"
  else
    printf '[缺]   %-12s %s\n' "${name}" "${hint}"
  fi
}

echo "Sesame Robot 环境检查"
echo "----------------------"
check "python3" "python3" "安装 Python 3.9+"
check "curl" "curl" "安装 curl"
check "git" "git" "安装 Git"
check "arduino-cli" "arduino-cli" "可选；用 Arduino IDE 图形界面也可烧录"

if command -v python3 >/dev/null 2>&1; then
  python3 - <<'PY'
import sys
print(f"[OK]   python 版本 {sys.version.split()[0]}")
PY
fi

echo
echo "下一步："
echo "  1. 按 docs/04-软件环境.md 安装 ESP32 开发板支持与库"
echo "  2. 按 software/tools/flash_notes.md 选择板型与烧录参数"
echo "  3. 连 AP 后测试：curl http://192.168.4.1/api/status"
