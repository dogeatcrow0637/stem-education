# firmware · Sesame Robot

> 适用版本：上游 `main` @ `f328ed35f5cbcbdd44a5e16e3ec679d2caa00d92`（2026-10-15）　|　最后更新：2026-11-05　|　前置条件：Arduino IDE 2.0+，已装 ESP32 开发板支持

## 为什么不复制源码

上游仓库约 230MB（含 STL/CAD/图片），且仍在活跃更新。本项目只记录**上游 commit + 文件清单 + 本地改动**，避免整包复制后无法合并上游修复。

```bash
mkdir -p ~/sesame && cd ~/sesame
git clone https://github.com/dorianborian/sesame-robot.git
cd sesame-robot
git checkout f328ed35f5cbcbdd44a5e16e3ec679d2caa00d92
```

## 文件结构

| 路径 | 作用 |
|---|---|
| `firmware/sesame-firmware-main.ino` | 主程序：PWM、WiFi、WebServer、API、姿态调度 |
| `firmware/face-bitmaps.h` | OLED 表情位图与 `FACE_LIST` 宏 |
| `firmware/movement-sequences.h` | 行走/姿态序列（`walk`、`wave`、`dance` 等） |
| `firmware/captive-portal.h` | 网页控制页 HTML/CSS/JS |
| `firmware/debugging-firmware/sesame-motor-tester.ino` | 单舵机测试与校准 |
| `motor-testing/README.md` | 电机测试/髋关节校准步骤 |

## 工具链

| 项 | 版本/设置 |
|---|---|
| IDE | Arduino IDE 2.0+ |
| 开发板支持 | ESP32 by Espressif Systems ≥ 2.0.0 |
| 库 | `ESP32Servo` **v3.0.9**（锁定） |
| 库 | `Adafruit SSD1306`、`Adafruit GFX Library` |
| 串口 | 115200 |

### 板型设置

| 板 | Board | 关键设置 |
|---|---|---|
| Lolin S2 Mini | **LOLIN S2 Mini** | Upload 921600；USB CDC On Boot = Enabled；Default 4MB with spiffs |
| Distro V3.1 | **ESP32S3 Dev Module** | USB CDC On Boot = Enabled；Flash Mode QIO 80MHz；Default 4MB with spiffs |
| Distro V2 | ESP32S3 Dev Module | 仅建议 USB 供电 |
| Distro V1 | ESP32 Dev Module | legacy，不推荐新做 |

### 引脚配置

打开主 `.ino`，只启用一组：

- S2 Mini：`servoPins = {1,2,4,6,8,10,13,14}`，SDA 33 / SCL 35。
- V3.1：`servoPins = {4,5,6,7,10,11,12,13}`，SDA 8 / SCL 9。
- V2/V1：见上游固件 README 的引脚表。

## 编译与烧录

1. 用 Arduino IDE 打开 `sesame-firmware-main.ino`（文件夹同名，全部 `.h` 同目录）。
2. 选择板型、端口、上表参数。
3. 点击 **Upload**。
4. 打开串口监视器 115200，复位，看到启动日志即成功。
5. 连 `Sesame-Controller-BETA`（密码 `12345678`），浏览器打开 `192.168.4.1`。

> 若上传失败：按住 BOOT 再 Upload；换能传数据的 USB-C 线；S3 板确认 USB CDC On Boot。

## 网络

| 模式 | 参数 |
|---|---|
| AP（默认） | SSID `Sesame-Controller-BETA`，密码 `12345678`，IP `192.168.4.1` |
| STA（可选） | 改 `NETWORK_SSID` / `NETWORK_PASS` / `ENABLE_NETWORK_MODE true`；或网页设置（不持久化） |
| mDNS | `http://sesame-robot.local` |

> ⚠️ 无鉴权、无 HTTPS；只在可信局域网使用。默认密码建议修改。

## API

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/api/status` | 当前命令、表情、网络状态 |
| POST | `/api/command` | JSON 下发动作/表情 |
| GET | `/cmd?go=forward` | 旧版移动 |
| GET | `/cmd?pose=wave` | 旧版姿态 |
| GET | `/cmd?motor=1&value=90` | 单舵机测试 |
| GET | `/getSettings` / `/setSettings?...` | 参数读写 |

```bash
curl http://192.168.4.1/api/status
curl -X POST http://192.168.4.1/api/command \
  -H "Content-Type: application/json" \
  -d '{"command":"wave","face":"happy"}'
```

完整命令/表情列表见上游 `firmware/README.md` 与 `movement-sequences.h`。

## 串口调试

- 波特率 115200。
- 电机测试固件支持 `0,90` 形式（舵机号,角度）与 `stop`。
- 主固件启动日志包含网络信息与 IP。
- 修改表情：`face-bitmaps.h`；加动作：`movement-sequences.h`；改网页：`captive-portal.h`。不要直接改主 `.ino` 的大段逻辑。

## 验证标准

- [ ] 串口有启动日志，无重启循环。
- [ ] OLED 亮，显示默认/待机表情。
- [ ] 热点出现，控制页可打开。
- [ ] `GET /api/status` 返回 JSON。
- [ ] `POST /api/command` 能让机器人挥手/换表情。
- [ ] 8 个舵机逐个测试方向正确，无堵转异响。

## 本地改动记录

当前**无本地代码改动**。如修改上游文件，在本节记录：文件、改动、原因、对应 commit；不要把整包源码复制进本目录。

## 上游参考

- 固件文档：https://github.com/dorianborian/sesame-robot/blob/main/firmware/README.md
- 电机测试：https://github.com/dorianborian/sesame-robot/blob/main/motor-testing/README.md
- 构建指南：https://github.com/dorianborian/sesame-robot/blob/main/docs/build-guide/README.md
