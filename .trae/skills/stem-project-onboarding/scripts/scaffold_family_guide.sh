#!/usr/bin/env bash
# 为某个 STEM 项目生成「家长实现」手册骨架
#
# 用法:
#   .trae/skills/stem-project-onboarding/scripts/scaffold_family_guide.sh <项目目录名> [--repo <仓库根>] [--force]
# 示例:
#   .trae/skills/stem-project-onboarding/scripts/scaffold_family_guide.sh rebot-devarm
#   .../scaffold_family_guide.sh line-follower-car --force
#
# 说明:
#   - 从 assets/家长实现手册模板/ 复制六个骨架文件到 projects/<slug>/docs/家长实现/
#   - 自动从 projects/<slug>/project.yaml 读取名称与编号，替换 {{PROJECT_NAME}} / {{PROJECT_SLUG}} / {{PROJECT_ID}} / {{DATE}}
#   - 默认不覆盖已存在文件（增量补教学线时保持已填写内容），加 --force 才覆盖
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKILL_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
TEMPLATE_DIR="${SKILL_DIR}/assets/家长实现手册模板"
GUIDE_DIR_NAME="家长实现"

usage() {
  echo "用法: $0 <项目目录名> [--repo <仓库根>] [--force]" >&2
  echo "示例: $0 rebot-devarm" >&2
  exit 1
}

SLUG=""
REPO=""
FORCE=0

while [ $# -gt 0 ]; do
  case "$1" in
    --repo) [ $# -ge 2 ] || usage; REPO="$2"; shift 2 ;;
    --force) FORCE=1; shift ;;
    -h|--help) usage ;;
    -*) echo "未知参数: $1" >&2; usage ;;
    *) if [ -z "${SLUG}" ]; then SLUG="$1"; else echo "多余参数: $1" >&2; usage; fi; shift ;;
  esac
done

[ -n "${SLUG}" ] || usage

# 定位仓库根：优先 --repo，否则从脚本位置向上找 templates/project-template 与 projects/
if [ -z "${REPO}" ]; then
  dir="${SCRIPT_DIR}"
  while [ "${dir}" != "/" ]; do
    if [ -d "${dir}/templates/project-template" ] && [ -d "${dir}/projects" ]; then
      REPO="${dir}"
      break
    fi
    dir="$(dirname "${dir}")"
  done
fi
if [ -z "${REPO}" ] || [ ! -d "${REPO}/templates/project-template" ] || [ ! -d "${REPO}/projects" ]; then
  echo "错误: 定位不到 stem-education 仓库根，请用 --repo <仓库根> 指定" >&2
  exit 1
fi
REPO="$(cd "${REPO}" && pwd)"

PROJECT_DIR="${REPO}/projects/${SLUG}"
if [ ! -d "${PROJECT_DIR}" ]; then
  echo "错误: 项目不存在: ${PROJECT_DIR}" >&2
  echo "提示: 先运行 ${REPO}/scripts/new-project.sh ${SLUG} \"<项目中文名>\"" >&2
  exit 1
fi
if [ ! -d "${TEMPLATE_DIR}" ]; then
  echo "错误: 找不到模板目录: ${TEMPLATE_DIR}" >&2
  exit 1
fi

# 从 project.yaml 读名称与编号
PROJECT_NAME=""
PROJECT_ID=""
if [ -f "${PROJECT_DIR}/project.yaml" ]; then
  # 去掉行内注释与首尾空白，再去掉包裹的引号
  PROJECT_NAME="$(sed -n 's/^name:[[:space:]]*//p' "${PROJECT_DIR}/project.yaml" | head -1 | sed 's/[[:space:]]*#.*$//; s/^[[:space:]]*//; s/[[:space:]]*$//; s/^"//; s/"$//')"
  PROJECT_ID="$(sed -n 's/^id:[[:space:]]*//p' "${PROJECT_DIR}/project.yaml" | head -1 | sed 's/[[:space:]]*#.*$//; s/^[[:space:]]*//; s/[[:space:]]*$//; s/^"//; s/"$//')"
fi
[ -n "${PROJECT_NAME}" ] || PROJECT_NAME="${SLUG}"
[ -n "${PROJECT_ID}" ] || PROJECT_ID="--"
TODAY="$(date +%F)"

GUIDE_DIR="${PROJECT_DIR}/docs/${GUIDE_DIR_NAME}"
mkdir -p "${GUIDE_DIR}"

# sed 替换串里 & 与 | 有特殊含义，先转义
esc() { printf '%s' "$1" | sed 's/[&|\\]/\\&/g'; }
SED_EXPR="s|{{PROJECT_NAME}}|$(esc "${PROJECT_NAME}")|g; s|{{PROJECT_SLUG}}|$(esc "${SLUG}")|g; s|{{PROJECT_ID}}|$(esc "${PROJECT_ID}")|g; s|{{DATE}}|$(esc "${TODAY}")|g"

replace_in() {
  local file="$1"
  if sed --version >/dev/null 2>&1; then
    sed -i "${SED_EXPR}" "${file}"
  else
    sed -i '' "${SED_EXPR}" "${file}"
  fi
}

created=0
skipped=0
for tpl in "${TEMPLATE_DIR}"/*.md; do
  [ -e "${tpl}" ] || continue
  base="$(basename "${tpl}")"
  dest="${GUIDE_DIR}/${base}"
  if [ -e "${dest}" ] && [ "${FORCE}" -ne 1 ]; then
    echo "  跳过（已存在）: docs/${GUIDE_DIR_NAME}/${base}"
    skipped=$((skipped + 1))
    continue
  fi
  cp "${tpl}" "${dest}"
  replace_in "${dest}"
  echo "  生成: docs/${GUIDE_DIR_NAME}/${base}"
  created=$((created + 1))
done

echo
echo "家长实现手册骨架已就绪: projects/${SLUG}/docs/${GUIDE_DIR_NAME}/"
echo "  项目: ${PROJECT_NAME}（编号 ${PROJECT_ID}）"
echo "  新建 ${created} 个，跳过 ${skipped} 个，日期 ${TODAY}"
echo
echo "后续待办:"
echo "  1. 逐文件把占位符 __ 替换为项目实际内容（禁止留 '待补' 空段）"
echo "  2. 同步工程文档 docs/01-项目简介.md ~ 05-教学与课程.md 与 99-参考资源.md"
echo "  3. 更新 ${REPO}/projects/README.md 索引与项目 CHANGELOG.md"
echo "  4. 补课件/儿童课程: scripts/scaffold_teaching_materials.sh ${SLUG}"
echo "  5. 按 SKILL.md 第 8 节自检清单逐条打勾后再提交"
