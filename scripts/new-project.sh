#!/usr/bin/env bash
# 从模板创建一个新的 STEM 项目骨架
#
# 用法:
#   ./scripts/new-project.sh <项目目录名> "<项目中文名>" [编号]
# 示例:
#   ./scripts/new-project.sh line-follower-car "循迹小车"
#
# 说明:
#   - 目录名请使用小写字母/数字/短横线
#   - 编号不传时自动取 projects/ 下现有项目的最大编号 +1（两位数字）
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
TEMPLATE_DIR="${ROOT_DIR}/templates/project-template"
PROJECTS_DIR="${ROOT_DIR}/projects"

usage() {
  echo "用法: $0 <项目目录名> \"<项目中文名>\" [编号]" >&2
  echo "示例: $0 line-follower-car \"循迹小车\"" >&2
  exit 1
}

[ $# -ge 2 ] || usage
SLUG="$1"
NAME="$2"

if ! echo "${SLUG}" | grep -Eq '^[a-z0-9][a-z0-9-]*$'; then
  echo "错误: 项目目录名只能用 小写字母/数字/短横线，且不能以短横线开头: ${SLUG}" >&2
  exit 1
fi

DEST="${PROJECTS_DIR}/${SLUG}"
if [ -e "${DEST}" ]; then
  echo "错误: 目标已存在: ${DEST}" >&2
  exit 1
fi

# 计算项目编号
if [ $# -ge 3 ]; then
  PID="$3"
else
  MAX=0
  for d in "${PROJECTS_DIR}"/*/; do
    [ -d "${d}" ] || continue
    base="$(basename "${d}")"
    # 只取 id 行、去掉行内注释与引号，再抽数字；强制十进制，避免 "02" 被当八进制
    raw="$(grep -E '^id:' "${d}project.yaml" 2>/dev/null | head -1)"
    raw="${raw%%#*}"
    n="$(printf '%s' "${raw}" | tr -dc '0-9')"
    n="${n:-0}"
    n=$((10#${n}))
    # 只认 1–99 的规整编号；异常值（如被旧脚本写坏的 13002）忽略，避免污染编号序列
    if [ "${n}" -ge 1 ] && [ "${n}" -le 99 ] && [ "${n}" -gt "${MAX}" ]; then MAX="${n}"; fi
  done
  PID=$(printf '%02d' $((MAX + 1)))
fi

cp -r "${TEMPLATE_DIR}" "${DEST}"

# 替换占位符（macOS/Linux 下的 sed -i 兼容处理）
# sed 替换串里 & 与 | 有特殊含义，先转义
esc() { printf '%s' "$1" | sed 's/[&|\\]/\\&/g'; }
NAME_ESC="$(esc "${NAME}")"
TODAY="$(date +%F)"
SED_EXPR="s|{{PROJECT_SLUG}}|${SLUG}|g; s|{{PROJECT_NAME}}|${NAME_ESC}|g; s|{{PROJECT_ID}}|${PID}|g; s|{{DATE}}|${TODAY}|g"

replace() {
  local file="$1"
  if sed --version >/dev/null 2>&1; then
    sed -i "${SED_EXPR}" "${file}"
  else
    sed -i '' "${SED_EXPR}" "${file}"
  fi
}
while IFS= read -r -d '' f; do replace "${f}"; done < <(find "${DEST}" -type f -print0)

echo "已创建项目: ${DEST}"
echo "  编号: ${PID}"
echo "  名称: ${NAME}"
echo
echo "后续待办:"
echo "  1. 编辑 projects/${SLUG}/project.yaml  （状态/标签/平台/许可）"
echo "  2. 编辑 projects/${SLUG}/README.md     （定位、参数、进度、快速开始）"
echo "  3. 在 projects/README.md 索引表中新增一行"
echo "  4. 填写 docs/01-项目简介.md 与 docs/99-参考资源.md"
echo "  5. 补齐 firmware/、software/*/ 的构建与运行说明"
