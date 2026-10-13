#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""离线回归：证明待办探针**不会自动重试**（不联网、不碰 Apple 端点）。

为什么要这个测试：
    "认证单次、绝不自动重试"是硬约束 —— Apple 按账号累加失败计数，
    连续重试就是自己把号锁掉。口头承诺没有约束力，这里把"默认一次"
    变成可断言的回归：**任何人往认证路径里加了重试循环，这个测试立刻红。**

原理：
    伪造 caldav / pyicloud 模块，客户端每次被构造就记一笔，并立刻抛异常。
    于是"记录的笔数" == "凭据提交次数"。全程不出本机、不访问网络。

跑：
    sidecar/runtime/Scripts/python.exe sidecar/tests/test_no_retry.py
退出码：0 通过 / 1 不通过。
"""

from __future__ import annotations

import contextlib
import io
import sys
import tempfile
import types
from pathlib import Path

HERE = Path(__file__).resolve()
REPO = HERE.parent.parent.parent          # sidecar/tests/x.py → 仓库根
sys.path.insert(0, str(REPO / "sidecar"))

# ── 伪造 caldav: 构造即记录 + 抛异常 ──────────────────────────────────
caldav_calls: list[str] = []


class _StubDAVClient:
    def __init__(self, *args, **kwargs):
        caldav_calls.append(str(kwargs.get("url")))
        raise RuntimeError("stub: refuse to touch the network")


_caldav = types.ModuleType("caldav")
_caldav.DAVClient = _StubDAVClient
sys.modules["caldav"] = _caldav

# ── 伪造 pyicloud: 构造即记录 + 抛"登录失败" ──────────────────────────
pyicloud_calls: list[str] = []


class _StubFailedLogin(Exception):
    pass


class _Stub2FARequired(Exception):
    pass


class _StubAuthRequired(Exception):
    pass


class _StubNotActivated(Exception):
    pass


class _StubPyiCloudService:
    def __init__(self, *args, **kwargs):
        pyicloud_calls.append(str(args[0] if args else kwargs.get("user")))
        raise _StubFailedLogin("stub: refuse to touch the network")


_pyicloud = types.ModuleType("pyicloud")
_pyicloud.PyiCloudService = _StubPyiCloudService
_pyicloud_exc = types.ModuleType("pyicloud.exceptions")
_pyicloud_exc.PyiCloud2FARequiredException = _Stub2FARequired
_pyicloud_exc.PyiCloudAuthRequiredException = _StubAuthRequired
_pyicloud_exc.PyiCloudFailedLoginException = _StubFailedLogin
_pyicloud_exc.PyiCloudServiceNotActivatedException = _StubNotActivated
sys.modules["pyicloud"] = _pyicloud
sys.modules["pyicloud.exceptions"] = _pyicloud_exc

import probe_reminders as pr  # noqa: E402


def count_caldav(china: bool | None, both: bool) -> list[str]:
    caldav_calls.clear()
    with contextlib.redirect_stdout(io.StringIO()):
        pr.probe_caldav("u@example.com", "pw", china=china, limit=1, probe_both=both)
    return list(caldav_calls)


def count_cloudkit(sessions: Path) -> list[str]:
    pyicloud_calls.clear()
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf), contextlib.redirect_stderr(buf):
        pr.probe_cloudkit("u@example.com", "pw", china=True, limit=1, sessions=sessions)
    return list(pyicloud_calls)


def main() -> int:
    sessions = Path(tempfile.mkdtemp(prefix="icloudmon-test-"))

    cases = [
        ("caldav 国内 默认", count_caldav, (True, False), 1),
        ("caldav 国际 默认", count_caldav, (False, False), 1),
        ("caldav 地区未知 默认", count_caldav, (None, False), 1),
        ("caldav 国内 --probe-both-hosts", count_caldav, (True, True), 2),
        ("cloudkit 默认", count_cloudkit, (sessions,), 1),
    ]

    passed = True
    for label, fn, args, expect in cases:
        got = len(fn(*args))
        ok = got == expect
        passed &= ok
        print(f"[{'OK ' if ok else 'FAIL'}] {label:34s} 凭据提交 {got} 次 (期望 {expect})")

    print()
    if passed:
        print("通过 —— 默认每条路最多 1 次凭据提交，只有显式 --probe-both-hosts 才是 2 次")
    else:
        print("不通过 —— 认证路径出现了额外提交，检查是否被加了重试循环")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
