# host · 服务端与网页控制台

> 适用版本：xiaozhi-esp32-server（2026-11）　|　最后更新：2026-11-13
> 本项目没有自研桌面程序；“上位机”= 语音服务端 + 可选的智控台网页。

## 1. 两种部署方式

| 方式 | 有什么 | 适合 | 端口 |
|---|---|---|---|
| 最简化（单服务） | WebSocket + OTA + 配置文件 | 家庭单设备，最快跑通 | 8000 / 8003 |
| 全模块 | 单服务 + MySQL + 智控台网页 | 多设备、多智能体、网页改配置 | 以仓库文档为准 |

> 家庭第一版用**最简化**；要给孩子做“单词本/多角色”再上全模块。

## 2. 最简化 Docker 部署

```bash
mkdir -p xiaozhi-server/data xiaozhi-server/models/SenseVoiceSmall
cd xiaozhi-server

# 从仓库下载 docker-compose.yml 与 config.yaml
# https://github.com/xinnan-tech/xiaozhi-esp32-server/blob/main/main/xiaozhi-server/docker-compose.yml
# https://github.com/xinnan-tech/xiaozhi-esp32-server/blob/main/main/xiaozhi-server/config.yaml
# 把要覆盖的配置写到 data/.config.yaml

docker compose up -d
docker logs -f xiaozhi-esp32-server
```

启动后日志应出现：

- OTA：`http://<IP>:8003/xiaozhi/ota/`
- WebSocket：`ws://<IP>:8000/xiaozhi/v1/`

## 3. 配置重点

| 配置 | 位置 | 说明 |
|---|---|---|
| 模型选择 | `selected_module` | ASR / LLM / TTS / Memory / Intent |
| 人设 | `prompt` | 必须换成英语老师，见 `../prompts/english-teacher.md` |
| 覆盖配置 | `data/.config.yaml` | 优先于 `config.yaml` |
| 智控台 | 全模块部署 | **启用后文件里的角色配置不生效** |
| 密钥 | `data/.config.yaml` / 环境变量 | 不提交、不截图 |

## 4. 验证

```bash
curl http://127.0.0.1:8003/xiaozhi/ota/    # 应返回 OTA 信息
docker logs --tail 50 xiaozhi-esp32-server # 无 traceback
```

设备端连上后，日志应出现 ASR 文本、LLM 请求、TTS 播放。

## 5. 常见问题

| 现象 | 原因 | 处理 |
|---|---|---|
| 启动即退出 | 缺模型/配置键错/端口占用 | 看日志第一行；补 `model.pt`；改端口 |
| 设备连不上 | IP 写错/防火墙/不同网段 | 同一路由器；关防火墙；`curl` OTA |
| TTS 超时 | EdgeTTS 网络/代理 | 换火山流式或关代理 |

## 6. 放到 ValleyServer 的云主机（vls）

> **关键：复用机器，不复用代码。** ValleyServer 仓库里的 `server/` 是《星旅人》的 Node 游戏存档 API，不能当语音服务端；xiaozhi-esp32-server 是独立的 Python 服务，和它**同机、不同目录、不同端口**运行。
> 状态：**待执行**（板子到货、家里 Wi-Fi 跑通后再上云）。

### 6.1 主机与端口

| 项 | 值 |
|---|---|
| 连接 | `ssh vls`（别名与机器信息见 ValleyServer `OPS.md`） |
| 公网 IP | `<服务器IP>`（腾讯云） |
| 规格 | 4 vCPU / 7.5 GiB / 178 GB（2026-11 已用约 14 GB） |
| 已有服务 | `xinglvren-api` `127.0.0.1:8787` + nginx `5800` + `clash` 7890–7893 |
| 本服务 | xiaozhi-server `127.0.0.1:8000`（WS）/ `127.0.0.1:8003`（OTA），nginx 对外 `5801` |
| 安全组 | **只新放行 5801**；8000/8003/8787 不对外 |

> 2026-11-13 部署前实测：可用内存 5.7 GiB、磁盘剩 151 GB、负载 0.10；部署后容器常驻 3.11 GiB，主机可用约 2.9 GiB。
> 资源快照见 [`服务器快照-2026-11-13.md`](服务器快照-2026-11-13.md)，部署结果与踩坑见 [`vls/部署记录-2026-11-13.md`](vls/部署记录-2026-11-13.md)。

**不要做的事**：不动 8787/5800/clash；不跑 `docker image prune -a`（OPS 记录过会误删基础镜像）；不把 Key 写进仓库或 nginx 配置。

### 6.2 目录与启动

```bash
ssh vls
mkdir -p ~/english-pet-watch/{data,models/SenseVoiceSmall}
cd ~/english-pet-watch

# 1. 放上游 docker-compose.yml / config.yaml（与家里同一套）
# 2. 下载 SenseVoiceSmall 模型到 models/SenseVoiceSmall/model.pt
# 3. 把本项目 software/configs/doubao-llm-only.config.yaml 填好 Key，写到 data/.config.yaml
chmod 600 data/.config.yaml

# 4. 只绑回环，避免绕过 nginx 直接暴露
#    docker-compose.yml 里端口写成 127.0.0.1:8000:8000 / 127.0.0.1:8003:8003
docker compose up -d
curl -s http://127.0.0.1:8003/xiaozhi/ota/    # 本机自检
```

### 6.3 nginx 反代（5801）

新建 `/etc/nginx/sites-available/english-pet-watch.conf`：

```nginx
server {
    listen 5801;
    server_name _;
    client_max_body_size 20m;

    location /xiaozhi/ota/ {
        proxy_pass http://127.0.0.1:8003;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
    }

    location /xiaozhi/v1/ {
        proxy_pass http://127.0.0.1:8000;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_read_timeout 3600s;
    }
}
```

```bash
sudo ln -sfn /etc/nginx/sites-available/english-pet-watch.conf /etc/nginx/sites-enabled/
sudo nginx -t && sudo systemctl reload nginx
```

对应配置：

```yaml
server:
  websocket: ws://<服务器IP>:5801/xiaozhi/v1/
```

固件 OTA 地址填：`http://<服务器IP>:5801/xiaozhi/ota/`。

### 6.4 上云前必须知道的三个坑

1. **明文 HTTP**：5801 没 TLS，孩子的语音会明文过公网。长期日用必须先上域名 + HTTPS/WSS；在没 TLS 前只做短时联调。国内域名是否要备案，以腾讯云当期要求为准（`待确认`）。
2. **EdgeTTS 出站**：在容器里测 `curl -I https://speech.platform.bing.com`；不通就换豆包 TTS，别私自去用主机上的 clash（那是别的项目）。
3. **资源**：FunASR + Python 服务约 1–2 GB 内存；先上「最简化」单服务，别同时起 MySQL 全模块。用 `docker stats` 看内存，和游戏 API 互不影响再长期跑。

### 6.5 验证与回滚

| 步骤 | 命令 / 判断 |
|---|---|
| 本机 OTA | `curl -s http://127.0.0.1:8003/xiaozhi/ota/` 有返回 |
| 公网 OTA | 外部 `curl -s http://<服务器IP>:5801/xiaozhi/ota/` 有返回 |
| 设备 | OTA 成功 → WebSocket 连上 → 说 Hello 有 ASR 文本 |
| 资源 | `docker stats --no-stream` 内存稳定、无重启 |
| 回滚 | `docker compose down` → 停用 nginx 站点 → 安全组关 5801 |

> 部署前先 `ssh vls 'docker ps -a; ss -lntup; df -h'` 确认端口和磁盘，再执行；不要把「同机」理解成「同一个进程」。
| 识别成日韩文 | ASR 模型/语种不对 | 补 SenseVoice 模型；换中英混识 ASR |
| 孩子说话被抢 | VAD 太灵敏 | `min_silence_duration_ms` 调到 900–1000 |
| 改了 Prompt 没变化 | 用了智控台 | 到智控台改智能体，不读配置文件 |
