#!/usr/bin/env python3
"""连接 Sesame、查状态、挥手、换表情、停止。

用法：
    python3 demo.py [base_url]
默认 base_url = http://192.168.4.1
"""

import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from sesame import Sesame, SesameError  # noqa: E402


def main() -> int:
    base = sys.argv[1] if len(sys.argv) > 1 else "http://192.168.4.1"
    robot = Sesame(base)
    try:
        print("状态:", robot.status())
        print("挥手 + 开心:", robot.command("wave", face="happy"))
        time.sleep(2)
        print("只换表情:", robot.face("excited"))
        time.sleep(1)
        print("停止:", robot.stop())
    except SesameError as exc:
        print("失败:", exc)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
