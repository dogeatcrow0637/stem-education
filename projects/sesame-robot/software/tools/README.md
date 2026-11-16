# tools · 开发机工具

| 文件 | 作用 |
|---|---|
| [`check_env.sh`](check_env.sh) | 检查 `python3`、`curl`、`git`、`arduino-cli` 是否存在（不安装） |
| [`flash_notes.md`](flash_notes.md) | Arduino IDE 板型/库/烧录参数速查 |

## 用法

```bash
bash projects/sesame-robot/software/tools/check_env.sh
```

脚本只读检查，不修改系统。缺少 `arduino-cli` 不影响用 Arduino IDE 图形界面烧录。

## 约定

- 破坏性操作（覆盖固件、清空 flash）必须先确认。
- 脚本幂等：重复执行不出错。
- 不读取/打印 WiFi 密码等敏感信息。
