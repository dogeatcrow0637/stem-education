# sdk · Python API 封装

本项目提供**最小可用**的 Python 封装，不追求完整覆盖上游 API。示例代码在 `../../code/api-examples/`。

## 安装

```bash
# 只用标准库时无需安装
python3 --version   # 3.9+
```

如使用 `requests` 版本：

```bash
python3 -m pip install requests
```

## 最小示例

```python
from sesame import Sesame

robot = Sesame("http://192.168.4.1")
print(robot.status())
robot.command("wave", face="happy")
robot.face("excited")
robot.stop()
```

## API

| 方法 | 参数 | 返回 | 说明 |
|---|---|---|---|
| `Sesame(base_url)` | `http://192.168.4.1` 或 `http://sesame-robot.local` | 实例 | 基地址 |
| `status()` | — | `dict` | `GET /api/status` |
| `command(command, face=None)` | `forward/backward/left/right/stop` 或姿态名 | `dict` | `POST /api/command` |
| `face(name, talking=False)` | 表情名；`talking=True` 自动加 `talk_` 前缀 | `dict` | 只换表情 |
| `stop()` | — | `dict` | 停止 |
| `settings()` | — | `dict` | `GET /getSettings` |
| `set_settings(**kwargs)` | `frameDelay` 等 | `dict` | `GET /setSettings` |

## 兼容矩阵

| SDK | 固件 | 硬件 | 备注 |
|---|---|---|---|
| `sdk v0.1` | 上游 `f328ed3` | S2 Mini / V3.1 | 本文档对应版本 |

## 约定

- 所有请求 3 秒超时；网络异常抛 `SesameError`。
- 不把 WiFi 密码写进代码；示例默认用 AP 地址。
- 机器人无鉴权，只在可信局域网调用。
