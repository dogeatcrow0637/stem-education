"""Sesame Robot 最小 Python API 封装（标准库，无第三方依赖）。

用途：连接机器人、读状态、下发动作/表情。
用法：
    from sesame import Sesame
    robot = Sesame("http://192.168.4.1")
    print(robot.status())
    robot.command("wave", face="happy")

注意：机器人固件默认无鉴权、无 HTTPS，只在可信局域网使用。
"""

from __future__ import annotations

import json
import socket
import urllib.error
import urllib.request
from typing import Any, Dict, Optional


class SesameError(RuntimeError):
    """网络或机器人返回错误。"""


class Sesame:
    def __init__(self, base_url: str = "http://192.168.4.1", timeout: float = 3.0) -> None:
        self.base_url = base_url.rstrip("/")
        self.timeout = timeout

    def _request(self, path: str, method: str = "GET", payload: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        url = f"{self.base_url}{path}"
        data = None
        headers = {"Accept": "application/json"}
        if payload is not None:
            data = json.dumps(payload).encode("utf-8")
            headers["Content-Type"] = "application/json"
        req = urllib.request.Request(url, data=data, headers=headers, method=method)
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as resp:
                body = resp.read().decode("utf-8")
        except (urllib.error.URLError, socket.timeout) as exc:
            raise SesameError(f"请求失败: {url} ({exc})") from exc
        try:
            return json.loads(body)
        except json.JSONDecodeError as exc:
            raise SesameError(f"返回不是 JSON: {body[:200]}") from exc

    def status(self) -> Dict[str, Any]:
        return self._request("/api/status")

    def command(self, command: str, face: Optional[str] = None) -> Dict[str, Any]:
        payload: Dict[str, Any] = {"command": command}
        if face:
            payload["face"] = face
        return self._request("/api/command", method="POST", payload=payload)

    def face(self, name: str, talking: bool = False) -> Dict[str, Any]:
        face = f"talk_{name}" if talking else name
        return self._request("/api/command", method="POST", payload={"face": face})

    def stop(self) -> Dict[str, Any]:
        return self.command("stop")

    def settings(self) -> Dict[str, Any]:
        return self._request("/getSettings")

    def set_settings(self, **kwargs: Any) -> Dict[str, Any]:
        query = "&".join(f"{key}={value}" for key, value in kwargs.items())
        return self._request(f"/setSettings?{query}")
