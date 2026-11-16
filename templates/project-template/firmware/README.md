# firmware · {{PROJECT_NAME}}

固件（单片机 / 主控板 / 驱动板）源码与烧录说明。

## 结构建议

```
firmware/
├── README.md          # 本文件
├── <主控名>/          # 一个主控一个子目录，如 main-board/、gripper/
│   ├── README.md      # 开发板型号、工具链版本、编译、烧录、验证
│   ├── src/           # 源码
│   ├── libs/          # 第三方库（注明来源与版本）
│   └── <工程文件>      # 如 platformio.ini / CMakeLists.txt / .uvprojx
└── tools/             # 烧录脚本、串口调试工具（可选）
```

## README 必备内容

| 项 | 说明 |
|---|---|
| 硬件 | 开发板/主控型号、晶振、外设引脚 |
| 工具链 | 编译器版本、IDE、烧录器（ST-Link / J-Link / esptool） |
| 编译 | 具体命令或 IDE 步骤 |
| 烧录 | 命令、接线、跳线/BOOT 设置 |
| 验证 | **怎么判断烧录成功**（如串口打印版本号、LED 状态、CAN 心跳） |
| 通信协议 | 对外接口（CAN / 串口 / Modbus / MQTT）的报文格式，建议附协议表 |

## 约定

- 不提交编译产物（`.hex`、`.bin`、`.elf`、`build/` 已被 `.gitignore` 忽略）。
- 第三方 SDK 用 git submodule 或 `libs/` + 版本说明，不要静默改动上游代码；如必须改，写在 `libs/PATCHES.md`。
- 版本号写入固件，便于上位机识别：`<项目>-<主控>-vX.Y.Z`。
