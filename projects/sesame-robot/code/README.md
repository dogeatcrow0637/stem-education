# code · 算法与实验代码

| 目录/文件 | 内容 |
|---|---|
| [`sesame.py`](sesame.py) | 最小 Python API 封装（标准库，无第三方依赖） |
| [`api-examples/demo.py`](api-examples/demo.py) | 连接 → 查状态 → 挥手 → 换表情 → 停止 |
| [`api-examples/walk_sequence.py`](api-examples/walk_sequence.py) | 连续动作序列示例 |

## 运行

```bash
# 先把 BASE 改成 http://192.168.4.1 或 http://sesame-robot.local
python3 projects/sesame-robot/code/api-examples/demo.py

# 指定地址
python3 projects/sesame-robot/code/api-examples/demo.py http://192.168.4.1
```

## 约定

1. 每个脚本顶部写用途、参数、依赖。
2. 只用标准库或锁定版本；不把 WiFi 密码写进代码。
3. 不提交数据集/权重；外部资源登记到 `../resources/README.md`。
4. 实验结论（哪个参数有效、现象如何）写回 `docs/05-教学与课程.md` 或本目录 README。
5. 引用上游代码注明 commit。

## 可做的实验

| 实验 | 改什么 | 观察什么 |
|---|---|---|
| 步态速度 | `frameDelay`、`walkCycles` | 走得更快/更稳？ |
| 启动电流 | `motorCurrentDelay` | 是否还会复位 |
| 表情联动 | `POST /api/command` 的 `face` | 动作与表情是否同步 |
| 定时提醒 | Python `time.sleep` + `command("wave")` | 到点挥手 |
| 动作序列 | 连续发 `wave`/`dance`/`stop` | 是否可中断、是否有竞态 |
