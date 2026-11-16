# software · 英语宠物怀表

> 适用版本：v1.0　|　最后更新：2026-11-13
> 本项目**不自研上位机 GUI**：语音服务端直接用 [xiaozhi-esp32-server](https://github.com/xinnan-tech/xiaozhi-esp32-server)（MIT），这里只放配置、提示词与工具说明。

| 子目录 | 内容 | 本项目用什么 |
|---|---|---|
| [`host/`](host/) | 服务端部署与网页控制台 | xiaozhi-esp32-server（Docker/源码）+ 智控台 |
| [`configs/`](configs/) | 可直接复制的 `data/.config.yaml` 模板 | 方案 A 零成本 / 方案 B 流式 |
| [`sdk/`](sdk/) | 二次开发接口 | 暂无自研 SDK；设备协议见固件文档 |
| [`tools/`](tools/) | 开发机脚本 | 烧录、环境自检、服务端启动命令 |
| [`prompts/`](prompts/) | 人格与教学提示词 | 英语老师 Prompt、情景脚本、单词表 |

## 快速开始

```bash
# 1. 服务端（最简化 Docker）
cp software/configs/mvp-free.config.yaml <服务端>/data/.config.yaml
#    填 <...> 占位符 + 放好 models/SenseVoiceSmall/model.pt
docker compose up -d

# 2. 固件（另开终端）
cd firmware
# 按 firmware/README.md 编译烧录
```

## 通用约定

1. 依赖锁定版本：服务端用 Docker 镜像/仓库 tag，不用 `latest` 漂移。
2. 已验证系统：macOS / Ubuntu 22.04（`待验证`）；烧录必须物理机 USB。
3. 密钥只放 `data/.config.yaml` 或环境变量，**不进仓库、不进截图、不发群**。
4. 英语老师人设只改 [`prompts/english-teacher.md`](prompts/english-teacher.md)，不要散落在多处。
5. 每次只改一条规则，录前后 10 秒对比，记录在 Prompt 文件末尾的「实验记录」。
