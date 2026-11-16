# firmware · 循迹小车

固件（Arduino 主控程序）源码与烧录说明。一个主控一个子目录。

## 目录

```
firmware/
├── README.md               # 本文件
└── line_follower/
    └── line_follower.ino   # 示例程序（UNO 用，逐行中文注释）
```

## 硬件

| 项 | 说明 |
|---|---|
| 主控 | Arduino UNO R3 兼容板（ATmega328P，16MHz） |
| 驱动 | L298N 双 H 桥模块 |
| 循迹 | 2 路 TCRT5000 红外反射模块 |
| 引脚 | ENA/ENB=D5/D6(PWM)；IN1–IN4=D7/D8/D9/D10；左探头=A0，右探头=A1 |

## 工具链

- Arduino IDE 2.x（开发板选 `Arduino Uno`），或 mBlock 5 / Mixly 2.0（积木版）
- 无第三方库依赖

## 编译与烧录

```
1) USB 线连接 UNO 与电脑
2) 打开 firmware/line_follower/line_follower.ino
3) IDE → 工具 → 开发板：Arduino Uno；端口：选对应的 USB 口
4) 点「上传」（向右箭头）
```

## 验证（怎么算烧录成功）

- 上传完成后 IDE 底部提示 `上传成功`；
- 打开**串口监视器**，波特率 **9600**，应每 200ms 打印一行：
  `L=xxx R=xxx | 状态: 直行`
- 把左/右探头分别对着**黑线**和**白底**，数值应明显不同 → 说明传感器与程序都活着。

## 调参（代码顶部常量）

| 常量 | 默认 | 怎么改 |
|---|---|---|
| `THRESHOLD` | 500 | 按实测：黑线读数与白底读数的中间值 |
| `BLACK_IS_LOW` | true | 若你的探头"黑线读数偏大"，改成 false |
| `SPEED_NORMAL` | 150 | 直行速度（0–255），跑太快掉线就调小 |
| `SPEED_TURN` | 110 | 转弯时慢轮速度 |

改完重新上传即可；每次只改一个量，方便对比。

## 约定

- 不提交编译产物（`.hex`/`.bin`/`build/` 已在 `.gitignore`）。
- 示例代码基于 Arduino IDE 自带例程改写，可自由用于教学。
