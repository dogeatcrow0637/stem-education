# sdk · reBot-DevArm 二次开发接口

> 状态：待开发。定位是**对官方 Python SDK 的薄封装**，让教学代码更简单，同时不破坏上游能力。

## 目标 API 形态（草案）

```python
from rebot import Arm

arm = Arm(interface="can0")        # 连接总线并扫描电机
arm.enable()                       # 使能（注意：无刹车，使能前请支撑）
state = arm.read_joints()          # 读取 7 个关节角度/速度/电流

arm.move_to([0, -30, 45, 0, 30, 0], gripper=0.5, speed=0.3)
arm.disable()

# 示教与数据集
with arm.record("demos/pick_cube") as rec:
    ...
```

## 设计原则

1. **不隐藏状态**：连接、使能、错误都要显式可见，教学场景需要“看得见发生了什么”。
2. **安全默认值**：默认低速度、默认限制运动范围；断使能前给出提示。
3. **兼容矩阵**：明确 `sdk 版本 ↔ 电机型号/固件 ↔ 硬件版本` 的对应关系。
4. **数据格式兼容 LeRobot**：示教数据导出直接产出标准 LeRobot 数据集，避免二次转换。

## 待填（开发后补）

- [ ] 最小可运行示例（连接 → 读状态 → 发一条指令）
- [ ] API 列表：类/函数、参数、返回值、异常
- [ ] 与官方 SDK / LeRobot 的关系说明（封装了什么、透传了什么）
- [ ] 兼容矩阵
