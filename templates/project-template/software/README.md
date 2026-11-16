# software · {{PROJECT_NAME}}

运行在电脑 / 开发机上的软件。

| 子目录 | 内容 | 典型技术栈 |
|---|---|---|
| [`host/`](host/) | 上位机：图形控制台、状态监控、参数配置 | Qt / PySide6 / Electron / Web |
| [`sdk/`](sdk/) | 对外二次开发接口 | Python 包 / C++ 库 / ROS 包 |
| [`tools/`](tools/) | 开发机工具：烧录、标定、仿真、环境初始化 | shell / Python 脚本 |

## 通用约定

1. 每个子项目自带 `README.md`：依赖、安装、运行、验证、常见问题。
2. 依赖必须锁定版本：Python 用 `requirements.txt` / `pyproject.toml`，Node 用 lock 文件。
3. 写明**已验证的操作系统**（例：Ubuntu 22.04 物理机）。若因 USB / 实时性 / 驱动限制**不能用虚拟机或容器**，必须在 README 用 ⚠️ 显著标注。
4. 禁止提交账号、内网 IP、密钥；本地配置走 `.env` / `local.yaml`（已被 `.gitignore` 忽略）。
5. 提供**一键环境脚本**（`tools/setup.sh`）能显著降低教学部署成本。
