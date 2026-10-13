#!/usr/bin/env python3
# ======================================================================
#  serve.py  -  常驻旁路进程 (方案 A 的 IPC 服务端)
#
#  协议: 一行一条 JSON (NDJSON), UTF-8。
#    stdin  收命令: {"id"?:n, "cmd":"...", ...}
#    stdout 发事件: {"evt":"ready|snapshot|error|reply", ...}
#
#  ★ 日志/报错一律走 stdout 的 JSON, 不往 stderr 写中文 ——
#    Qt 侧用 QProcess 读通道, stderr 混入会在中文系统上被 GBK 截断,
#    也会把"数据"和"诊断"两件事混在一起。
#
#  命令一览:
#    {"cmd":"refresh"}                        立即采样一次
#    {"cmd":"start","interval":5}             启动周期采样(秒)
#    {"cmd":"stop"}                           停止周期采样
#    {"cmd":"icloud","action":"start|stop|restart"}
#    {"cmd":"setting.get","key":"driveRoot"}
#    {"cmd":"setting.set","key":"driveRoot","value":"C:/..."}
#    {"cmd":"setting.set","key":"ui/closeToTray","value":true}
#    {"cmd":"history.reset"}                  清空卡死检测基线
#    {"cmd":"quit"}                           退出
# ======================================================================
from __future__ import annotations

import json
import os
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from icloudmon import paths                                   # noqa: E402
from icloudmon.bridge import build                            # noqa: E402
from icloudmon.procs import running_processes                 # noqa: E402
from icloudmon import service                                 # noqa: E402
from icloudmon.snapshot import sample                         # noqa: E402
from icloudmon.stall import append_history, detect_stall, reset_history  # noqa: E402

DEFAULT_REFRESH_SEC = 5
DEFAULT_STALL_SEC = 300

_lock = threading.RLock()
_state = {
    "refresh_sec": DEFAULT_REFRESH_SEC,
    "stall_sec": DEFAULT_STALL_SEC,
    "running": False,
    "restarting": False,
    "last": None,          # 最近一次 payload (供 setting 变更后即时重发)
}
_stop = threading.Event()
# 采样间隔变更 / 需要立即采样的唤醒信号。
# ★ 不能只用 time.sleep(interval): 睡眠时长是在**上一轮**算的,
#   刚发 start(interval=1) 时采样线程可能正睡在旧的 5 秒里,
#   表现为"命令确认了但界面几秒不刷新"。用 Event 唤醒才能即时生效。
_wake = threading.Event()


def emit(evt: str, data=None, req_id=None) -> None:
    line = {"evt": evt}
    if req_id is not None:
        line["id"] = req_id
    if data is not None:
        line["data"] = data
    sys.stdout.write(json.dumps(line, ensure_ascii=False) + "\n")
    sys.stdout.flush()


def do_sample(push: bool = True) -> dict:
    s = sample()
    append_history(s)                      # 历史 baseline: 卡死检测的依据
    st = detect_stall(_state["stall_sec"])
    payload = build(
        s, st,
        running=running_processes(),
        refresh_sec=_state["refresh_sec"],
        stall_sec=_state["stall_sec"],
        close_to_tray=bool(paths.setting_get("ui/closeToTray", False)),
        restarting=_state["restarting"],
    )
    with _lock:
        _state["last"] = payload
    if push:
        emit("snapshot", payload)
    return payload


def _sampler() -> None:
    while not _stop.is_set():
        with _lock:
            running = _state["running"]
            interval = _state["refresh_sec"]
        if running:
            try:
                do_sample()
            except Exception as e:                    # noqa: BLE001
                emit("error", {"message": f"采样失败: {e}"})
            _wake.clear()
            _wake.wait(interval)
        else:
            # 空闲时也阻塞在 Event 上, 让 start 能立刻唤醒, 不做忙等
            _wake.clear()
            _wake.wait(1.0)


def handle(cmd: dict) -> None:
    # ★ 必须是**单条** if/elif 链, 每个分支各自 return。
    #   之前写成两段独立的 if 链, 前面命中的命令会继续落到第二段的
    #   "未知命令" 兜底里 —— 表现为 start/ping 都合法却回 error。
    name = cmd.get("cmd")
    rid = cmd.get("id")

    if name == "refresh":
        emit("reply", do_sample(push=False), rid)

    elif name == "start":
        with _lock:
            _state["refresh_sec"] = max(1, min(3600, int(cmd.get("interval", 5))))
            _state["running"] = True
            sec = _state["refresh_sec"]
        emit("reply", {"ok": True, "refreshSec": sec}, rid)
        _wake.set()          # 立即开始采样, 不等旧间隔走完

    elif name == "stop":
        with _lock:
            _state["running"] = False
        emit("reply", {"ok": True}, rid)
        do_sample()          # 停之前再推一帧, 保证界面数字是最终的

    elif name == "icloud":
        action = cmd.get("action", "")
        with _lock:
            _state["restarting"] = True
        emit("reply", {"ok": True, "action": action}, rid)
        try:
            if action == "stop":
                ok, msg = service.stop_all()
            elif action == "start":
                ok, msg = service.start_all()
            elif action == "restart":
                ok, msg = service.restart()
            else:
                ok, msg = False, f"未知动作: {action}"
        except Exception as e:                        # noqa: BLE001
            ok, msg = False, f"操作异常: {e}"
        with _lock:
            _state["restarting"] = False
        emit("icloudResult", {"ok": ok, "message": msg})
        do_sample()                                   # 状态立即回传界面

    elif name == "setting.get":
        emit("reply", {"value": paths.setting_get(str(cmd.get("key")))}, rid)

    elif name == "setting.set":
        ok = paths.setting_set(str(cmd.get("key")), cmd.get("value"))
        emit("reply", {"ok": ok}, rid)
        if ok:
            do_sample()

    elif name == "history.reset":
        reset_history()
        emit("reply", {"ok": True}, rid)

    elif name == "ping":
        emit("reply", {"ok": True, "ts": time.time()}, rid)

    elif name == "quit":
        emit("reply", {"ok": True}, rid)
        _stop.set()

    else:
        emit("error", {"message": f"未知命令: {name}"}, rid)


def main() -> int:
    emit("ready", {
        "pid": os.getpid(),
        "driveRoot": paths.drive_root(),
        "dbFile": paths.db_file(),
        "refreshSec": DEFAULT_REFRESH_SEC,
        "stallSec": DEFAULT_STALL_SEC,
    })
    threading.Thread(target=_sampler, daemon=True).start()

    for raw in sys.stdin:                  # 阻塞读命令, 直到 stdin 关闭
        line = raw.strip()
        if not line:
            continue
        try:
            cmd = json.loads(line)
        except json.JSONDecodeError:
            emit("error", {"message": "命令不是合法 JSON"})
            continue
        if not isinstance(cmd, dict):
            emit("error", {"message": "命令必须是 JSON 对象"})
            continue
        try:
            handle(cmd)
        except Exception as e:                          # noqa: BLE001
            emit("error", {"message": f"命令处理失败: {e}"}, cmd.get("id"))
        if _stop.is_set():
            break

    _stop.set()
    emit("bye", {"pid": os.getpid()})
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
