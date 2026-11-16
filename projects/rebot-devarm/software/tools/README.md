# tools · reBot-DevArm 开发机工具

> 状态：待开发。**建议最先动手的就是这里**——先能“看到设备”，才谈得上控制。

## 计划脚本

| 脚本 | 作用 | 状态 |
|---|---|---|
| `setup.sh` | 安装依赖、写入 udev 规则、配置 CAN 接口（幂等） | 待写 |
| `check-env.sh` | 环境自检：系统版本、CAN 设备、权限、依赖版本 | 待写 |
| `can-scan.py` | 扫描 CAN 总线上的电机，输出 ID / 型号 / 在线状态 | 待写 |
| `set-motor-id.py` | 电机 ID 设置与校验（⚠️ 破坏性，需二次确认） | 待写 |
| `calibrate.py` | 零位/方向标定，输出 `calib.yaml` | 待写 |
| `flash-motor.sh` | 电机固件升级（如厂商支持） | 待确认 |

## udev 规则示例（固定 CAN 适配器名称）

```
# /etc/udev/rules.d/90-rebot-can.rules
SUBSYSTEM=="net", ACTION=="add", ATTRS{idVendor}=="<VID>", ATTRS{idProduct}=="<PID>", NAME="can0"
```

写入后 `sudo udevadm control --reload && sudo udevadm trigger`。

## 约定

- 脚本顶部写清「用途 / 用法 / 依赖 / 影响范围」；**破坏性操作必须二次确认**（改 ID、写参数、升级固件）。
- 幂等：重复执行不报错、不重复安装。
- 版本号读同一份来源（如 `VERSION`），不要在多个脚本里硬编码。
