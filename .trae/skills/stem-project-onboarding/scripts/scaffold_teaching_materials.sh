#!/usr/bin/env bash
# 为某个 STEM 项目生成「儿童课程」骨架（按课时分目录）
#
# 用法:
#   .trae/skills/stem-project-onboarding/scripts/scaffold_teaching_materials.sh <项目目录名> [--repo <仓库根>] [--force] [--lesson <NN>] [--title <课题>]
# 示例:
#   .../scaffold_teaching_materials.sh rebot-devarm
#   .../scaffold_teaching_materials.sh line-follower-car --force
#   .../scaffold_teaching_materials.sh cyberbrick --lesson 02 --title 让打印机吐出我的零件
#
# 说明:
#   - 从 assets/ 复制骨架到 projects/<slug>/docs/儿童课程/，产出结构：
#       docs/儿童课程/README.md                      课程总览（课时表 + 知识卡全集索引）
#       docs/儿童课程/第NN课-课题/README.md          课时卡（本课材料清单 + 配套位置 + 重建命令）
#       docs/儿童课程/第NN课-课题/课件.md            课件源（Marp 风格，md → 课件.html）
#       docs/儿童课程/第NN课-课题/图/                本课配图（构建时自动内嵌）
#       docs/儿童课程/第NN课-课题/知识卡/NN-主题.md  本课发的知识卡（一个原理点一张）
#   - 替换 {{PROJECT_NAME}} / {{PROJECT_SLUG}} / {{PROJECT_ID}} / {{DATE}} / {{LESSON_NO}} / {{LESSON_TITLE}}
#   - 默认不覆盖已存在文件（增量补教学线时保留已填写内容），加 --force 才覆盖
#   - 加课时：复制 `第01课-第一课/` 为 `第02课-<课题>/`，再在 儿童课程/README.md 与 docs/05 登记
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKILL_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
ASSET_DIR="${SKILL_DIR}/assets"

usage() {
  echo "用法: $0 <项目目录名> [--repo <仓库根>] [--force] [--lesson <NN>] [--title <课题>]" >&2
  echo "示例: $0 rebot-devarm" >&2
  exit 1
}

SLUG=""
REPO=""
FORCE=0
LESSON_NO="01"
LESSON_TITLE="第一课"

while [ $# -gt 0 ]; do
  case "$1" in
    --repo) [ $# -ge 2 ] || usage; REPO="$2"; shift 2 ;;
    --force) FORCE=1; shift ;;
    --lesson) [ $# -ge 2 ] || usage; LESSON_NO="$2"; shift 2 ;;
    --title) [ $# -ge 2 ] || usage; LESSON_TITLE="$2"; shift 2 ;;
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
for f in "课件模板/课件骨架.md" "知识卡模板.md" "课程模板/课时README.md" "课程模板/课程总览README.md"; do
  [ -f "${ASSET_DIR}/${f}" ] || { echo "错误: 找不到模板 ${ASSET_DIR}/${f}" >&2; exit 1; }
done

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
LESSON_DIR="第${LESSON_NO}课-${LESSON_TITLE}"

# sed 替换串里 & 与 | 有特殊含义，先转义
esc() { printf '%s' "$1" | sed 's/[&|\\]/\\&/g'; }
SED_EXPR="s|{{PROJECT_NAME}}|$(esc "${PROJECT_NAME}")|g; s|{{PROJECT_SLUG}}|$(esc "${SLUG}")|g; s|{{PROJECT_ID}}|$(esc "${PROJECT_ID}")|g; s|{{DATE}}|$(esc "${TODAY}")|g; s|{{LESSON_NO}}|$(esc "${LESSON_NO}")|g; s|{{LESSON_TITLE}}|$(esc "${LESSON_TITLE}")|g"

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
emit() { # emit <模板相对路径> <目标相对路径>
  local tpl="${ASSET_DIR}/$1" dest="${PROJECT_DIR}/$2"
  mkdir -p "$(dirname "${dest}")"
  if [ -e "${dest}" ] && [ "${FORCE}" -ne 1 ]; then
    echo "  跳过（已存在）: $2"
    skipped=$((skipped + 1))
    return
  fi
  cp "${tpl}" "${dest}"
  replace_in "${dest}"
  echo "  生成: $2"
  created=$((created + 1))
}

emit "课程模板/课程总览README.md" "docs/儿童课程/README.md"
emit "课程模板/课时README.md"     "docs/儿童课程/${LESSON_DIR}/README.md"
emit "课件模板/课件骨架.md"        "docs/儿童课程/${LESSON_DIR}/课件.md"
emit "知识卡模板.md"              "docs/儿童课程/${LESSON_DIR}/知识卡/${LESSON_NO}-知识卡-待命名.md"
mkdir -p "${PROJECT_DIR}/docs/儿童课程/${LESSON_DIR}/图"
[ -e "${PROJECT_DIR}/docs/儿童课程/${LESSON_DIR}/图/.gitkeep" ] || : > "${PROJECT_DIR}/docs/儿童课程/${LESSON_DIR}/图/.gitkeep"

echo
echo "儿童课程骨架已就绪: projects/${SLUG}/docs/儿童课程/"
echo "  项目: ${PROJECT_NAME}（编号 ${PROJECT_ID}）"
echo "  本课: ${LESSON_DIR}"
echo "  新建 ${created} 个，跳过 ${skipped} 个，日期 ${TODAY}"
echo
echo "后续待办:"
echo "  1. 填占位符 __（禁止留空段），课件最后一页必须是通关庆祝页"
echo "  2. 加课时: 复制 docs/儿童课程/${LESSON_DIR}/ → 第NN课-<课题>/，并在 儿童课程/README.md 与 docs/05 登记"
echo "  3. 每个原理点建一张知识卡（六维里出现的术语不得缺卡），放本课 知识卡/"
echo "  4. 构建并自检课件:"
echo "     python3 ${SKILL_DIR}/scripts/build_slides.py ${PROJECT_DIR}/docs/儿童课程/*/课件.md --strict"
echo "  5. 参考样例与示例图: ${ASSET_DIR}/课件模板/示例-第一课.md"
echo "  6. 同步 docs/01~05、99 与家长实现手册，更新 ${REPO}/projects/README.md 索引与 CHANGELOG.md"
