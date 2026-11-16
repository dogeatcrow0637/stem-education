#!/usr/bin/env python3
"""连续动作序列示例：站起 → 挥手 → 跳舞 → 趴下。

用法：
    python3 walk_sequence.py [base_url]
注意：确保机器人周围 50cm 无障碍，手离开运动范围。
"""

import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from sesame import Sesame, SesameError  # noqa: E402

SEQUENCE = [
    ("stand", "default", 2.0),
    ("wave", "happy", 3.0),
    ("dance", "excited", 4.0),
    ("rest", "sleepy", 2.0),
]


def main() -> int:
    base = sys.argv[1] if len(sys.argv) > 1 else "http://192.168.4.1"
    robot = Sesame(base)
    try:
        for command, face, wait in SEQUENCE:
            print(f"执行 {command} / {face}")
            robot.command(command, face=face)
            time.sleep(wait)
        print("结束")
    except SesameError as exc:
        print("失败:", exc)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
