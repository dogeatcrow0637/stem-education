# 大文件与 Git LFS

STEM 项目天然会产生大文件：3D 网格、装配 STEP、演示视频、数据集、模型权重。原则是：

> **可重新生成的、体积大的、Git 存不动的，都不要直接进普通 Git 提交。**

## 1. 分类处理

| 类型 | 典型体积 | 处理方式 |
|---|---|---|
| 文本、源码、Markdown、CSV、接线图 PDF | < 10 MB | 直接进 Git |
| 单个零件 STL / STEP | 1–30 MB | 尽量保留源文件进 Git；确实需要时用 LFS |
| 整套装配 STEP / 复杂曲面 | 几十 MB – 数百 MB | **Git LFS** 或外链 |
| 切片文件 `.gcode` / `.3mf` | 10–200 MB | **不入库**（已 `.gitignore`，参数记录在 `print-notes.md` 中） |
| 演示视频、数据集、模型权重 | > 100 MB | 外链网盘 / 对象存储，在 `resources/README.md` 登记链接 + SHA256 |

## 2. Git LFS 启用方式

本仓库**默认未开启 LFS**（避免未安装 lfs 的环境提交报错）。需要时按下面步骤启用：

```bash
# 1. 安装（一次性）
brew install git-lfs
git lfs install

# 2. 在 stem-education/ 目录下声明要纳管的类型
cd stem-education
git lfs track "*.step" "*.stp" "*.stl" "*.3mf" "*.zip" "*.mp4" "*.bag"

# 3. 生成的 .gitattributes 需要一并提交
git add .gitattributes
git commit -m "chore(stem): 启用 Git LFS 纳管三维与视频文件"
```

> `git lfs track` 生成的 `.gitattributes` 只对**其所在目录及子目录**生效。放在 `stem-education/` 根目录下即可覆盖全部项目。

## 3. 外链登记的写法

不入库的大文件，在 `resources/README.md` 中按下表登记，做到「链接可查、内容可验」：

| 文件 | 用途 | 位置 | 大小 | SHA256 | 备注 |
|---|---|---|---|---|---|
| `demo-2026-09.mp4` | 组装全过程录像 | 网盘链接 | 120 MB | `abcd…` | 官方频道，随时可下 |

## 4. 已经误提交大文件怎么办

```bash
# 尚未推送：直接撤销该次提交并重新组织
git reset --soft HEAD~1

# 已推送历史很久：使用 git filter-repo 或 BFG 清理后再强推（需团队协调）
# 简单做法：先 git rm --cached 该文件 + 写进 .gitignore，历史上仍保留，后续不再增长
git rm --cached projects/<项目>/enclosure/step/big-assembly.step
```

## 5. 体积红线

- 单次提交增量建议 **< 20 MB**。
- 仓库总体积超过 **1 GB** 时应考虑把素材类内容整体迁出（独立素材仓或对象存储）。
