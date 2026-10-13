#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""待办(Reminders)取数探针 —— **只读**。

目的只有一个: 用最小代价回答「你的账号到底能不能拿到提醒事项, 走哪条路」。
不写任何数据, 不改主程序, 不产生副作用(除了本地会话缓存)。

两条路各探一次:

  路 B  caldav
      标准 CalDAV (RFC 4791) + Apple **App 专用密码**。
      优点: 零 2FA、口令可一键吊销、协议公开稳定、库成熟。
      风险: iOS 13 起 Apple 把提醒事项迁到 CloudKit, 只有**没被升级过**的
            旧清单还留在 CalDAV 上 —— 有些账号一个清单都没有。必须实测。

  路 A  cloudkit
      pyicloud 原生登录 (SRP-6a + 2FA) + CloudKit Web Services。
      优点: 完整读写 + 增量游标, 与 iOS 看到的完全一致。
      风险: 私有 Web API, Apple 改版会断(故锁版本 + 保留探针)。

用法:
    # 路 B (推荐先试: 成本最低, 不用交主号密码)
    python sidecar/probe_reminders.py caldav --user you@icloud.com

    # 路 A
    python sidecar/probe_reminders.py cloudkit --user you@icloud.com

    # 两条都探
    python sidecar/probe_reminders.py both --user you@icloud.com

口令来源(按优先级):
    1) 环境变量 ICLOUDMON_APP_PASSWORD(路 B) / ICLOUDMON_PASSWORD(路 A)
    2) 交互输入(getpass, 不回显)

    ⚠ 刻意**不提供** --password / --app-password 命令行参数:
      命令行会进 PowerShell 历史(PSReadLine)与进程命令行(WMI 可查),
      等于把口令写进本机日志。要非交互就用环境变量, 且用完即删:
      Remove-Item Env:ICLOUDMON_PASSWORD

参数:
    --china / --no-china   强制国内(云上贵州)或国际版; 默认自动探测
    --limit N              每个清单最多取几条(默认 5, 只为看数据形状)
    --sessions DIR         会话缓存目录(默认 sidecar/runtime/.sessions)
    --probe-both-hosts     路 B: 两个 CalDAV host 都试(默认只试探测到的一个)
    --clear-sessions       删除会话缓存(等价"退出登录")后退出

安全与风控契约(硬约束, 改动前先读):
    1. 口令**不落盘、不进日志**。SRP 只把口令的 sha256 派生证明发给 Apple,
       明文不出本机; 本脚本只打印掩码, 不写任何文件。落盘的只有**登录成功后**
       的会话令牌(.sessions/), 那是等价凭据不是口令。
    2. 认证**单次**: 失败立即退出, 绝不自动重试 —— 无论毫秒级循环还是几十上百次。
       Apple 侧的失败计数按账号累加, 连续重试 = 自己把账号锁掉。
       失败后**不要连续重跑本脚本**; 先看错误(口令/地区/网络), 改对了再跑一次。
    3. 会消耗认证尝试次数的动作只有两处: 路 B 一次 host 登录(默认单 host),
       路 A 一次 SRP 登录。--probe-both-hosts 会把路 B 变成最多两次,
       仅在怀疑地区判断错了时才用。
    4. .sessions/ 里是**等价凭据**(session_id + trust_token, 数周内可免 2FA)。
       该目录不进版本库; 想"退出登录"就 --clear-sessions 或直接删目录。

退出码: 0 = 至少一条路可用; 1 = 都不可用。
"""

from __future__ import annotations

import argparse
import getpass
import os
import plistlib
import shutil
import sys
from pathlib import Path

# ── 让中文在任意终端都不乱码 ───────────────────────────────────────────
try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

HERE = Path(__file__).resolve().parent
DEFAULT_SESSIONS = HERE / "runtime" / ".sessions"

CALDAV_HOSTS = {
    "intl": "https://caldav.icloud.com/",
    "cn": "https://caldav.icloud.com.cn/",
}

BAR = "=" * 72


def hr(title: str = "") -> None:
    print()
    print(BAR)
    if title:
        print(title)
        print(BAR)


def mask(secret: str) -> str:
    """口令只露头尾, 便于核对但不泄露。"""
    if not secret:
        return "(空)"
    if len(secret) <= 4:
        return "*" * len(secret)
    return f"{secret[:2]}{'*' * (len(secret) - 4)}{secret[-2:]}"


# ======================================================================
#  国内账号探测
# ======================================================================
def detect_china_mainland() -> bool | None:
    """从 iCloud for Windows 的 iCloudWinPref.plist 里看 quota host 是不是
    *.icloud.com.cn —— 这是"云上贵州"账号的可靠本地证据(非推测)。

    返回 True/False, 读不到返回 None(交给 pyicloud 自动判断)。
    """
    local = os.environ.get("LOCALAPPDATA")
    if not local:
        return None
    pref = (
        Path(local)
        / "Packages"
        / "AppleInc.iCloud_nzyj5cx40ttqa"
        / "LocalCache"
        / "Roaming"
        / "Apple Computer"
        / "Preferences"
        / "iCloudWinPref.plist"
    )
    try:
        with open(pref, "rb") as f:
            data = plistlib.load(f)
    except Exception:
        return None

    blob = repr(data)
    if "icloud.com.cn" in blob:
        return True
    if "icloud.com" in blob:
        return False
    return None


# ======================================================================
#  路 B —— CalDAV
# ======================================================================
def probe_caldav(user: str, password: str, *, china: bool | None, limit: int,
                 probe_both: bool = False) -> bool:
    hr("路 B · CalDAV (RFC 4791) + App 专用密码")

    try:
        import caldav
    except ImportError:
        print("  [跳过] 未安装 caldav。先跑:")
        print("         sidecar/runtime/Scripts/python.exe -m pip install caldav")
        return False

    print(f"  账号   : {user}")
    print(f"  口令   : {mask(password)}")
    print(f"  地区   : {'国内(云上贵州)' if china else '国际版' if china is False else '自动'}")
    print()

    # 默认**只打一个 host**: 每多打一个 host 就多交一次凭据, 而连续的失败登录
    # 正是 Apple 风控锁号的触发条件。--probe-both-hosts 才补打第二个。
    if probe_both:
        order = ["cn", "intl"] if china else ["intl", "cn"]
        print("  ! --probe-both-hosts 已开: 最多两次登录尝试(仅地区判断存疑时用)")
    elif china is True:
        order = ["cn"]
    else:
        order = ["intl"]

    client = None
    for key in order:
        url = CALDAV_HOSTS[key]
        print(f"  → 尝试 {url} ... ", end="", flush=True)
        try:
            c = caldav.DAVClient(url=url, username=user, password=password, timeout=30)
            principal = c.principal()
            name = ""
            try:
                name = principal.get_display_name() or ""
            except Exception:
                pass
            print(f"OK  (principal={name or '匿名'})")
            client = c
            break
        except Exception as e:
            print(f"失败: {type(e).__name__}: {str(e)[:120]}")

    if client is None:
        print()
        print("  结论: ✗ CalDAV 不可用(目标 host 连不上)")
        print("        常见原因: 口令不是 App 专用密码 / 地区判断错 / 账号被锁 / 网络到 Apple 被拦")
        print("        ⚠ 不要连续重跑。先核对上面那条错误; 只有确认地区可能判错,")
        print("          才单独跑一次 --probe-both-hosts。反复重试只会累加失败计数。")
        return False

    # ── 找支持 VTODO 的集合(提醒事项清单) ──
    try:
        calendars = client.principal().calendars()
    except Exception as e:
        print(f"  结论: ✗ 枚举集合失败: {type(e).__name__}: {str(e)[:140]}")
        return False

    print(f"  找到 {len(calendars)} 个日历集合")

    task_lists = []
    for cal in calendars:
        try:
            # with_fallback=False: 严格按服务器声明判断, 不用库的默认兜底,
            # 否则普通日历会被误判成任务清单
            comps = cal.get_supported_components(with_fallback=False)
        except Exception:
            comps = []
        comps_u = [str(c).upper() for c in (comps or [])]
        display = ""
        try:
            display = cal.get_display_name() or ""
        except Exception:
            pass
        is_task = "VTODO" in comps_u
        print(f"    - {display or '(无名)'}  components={comps_u or '(未声明)'}"
              f"{'   ← 提醒事项清单' if is_task else ''}")
        if is_task:
            task_lists.append((cal, display))

    print()
    if not task_lists:
        print("  结论: ✗ CalDAV 连得上, 但**没有任何 VTODO 清单**。")
        print("        说明你的提醒事项已经全部迁到 CloudKit(iOS 13 之后的默认行为),")
        print("        CalDAV 这条路对你账号无效 —— 转路 A (cloudkit)。")
        return False

    # ── 读条目(只读) ──
    print(f"  读取前 {min(len(task_lists), 20)} 个清单的条目(每清单最多 {limit} 条):")
    total = 0
    for cal, display in task_lists[:20]:
        try:
            # 含已完成项: 只看未完成的话, 全部做完的清单会显示 0 条, 容易误判
            todos = cal.get_todos(include_completed=True) or []
        except Exception as e:
            print(f"    - {display}: 读取失败 {type(e).__name__}: {str(e)[:100]}")
            continue

        def _comp(t):
            c = getattr(t, "icalendar_component", None)
            if c is None:
                inst = getattr(t, "icalendar_instance", None)
                cs = inst.walk("VTODO") if inst is not None else []
                c = cs[0] if cs else None
            return c

        parsed = [(t, _comp(t)) for t in todos]
        total += len(parsed)
        done = sum(1 for _, c in parsed
                   if c is not None and str(c.get("STATUS", "")).upper() == "COMPLETED")
        suffix = f" (已完成 {done})" if done else ""
        print(f"    - {display}: {len(parsed)} 条{suffix}")
        for _, c in parsed[:limit]:
            summary = str(c.get("SUMMARY", "(无标题)")) if c is not None else "(解析失败)"
            status = str(c.get("STATUS", "")) if c is not None else ""
            due = str(c.get("DUE", "")) if c is not None else ""
            print(f"        · {summary}"
                  f"{'  [' + status + ']' if status else ''}"
                  f"{'  due=' + due if due else ''}")

    print()
    print(f"  结论: ✓ CalDAV 可用 —— {len(task_lists)} 个提醒事项清单, 共 {total} 条")
    print("        这条路零 2FA、口令可随时吊销, 优先用它。")
    return True


# ======================================================================
#  路 A —— pyicloud / CloudKit
# ======================================================================
def probe_cloudkit(user: str, password: str, *, china: bool | None,
                   limit: int, sessions: Path) -> bool:
    hr("路 A · pyicloud 原生登录 (SRP-6a + 2FA) + CloudKit")

    try:
        from pyicloud import PyiCloudService
        from pyicloud.exceptions import (
            PyiCloud2FARequiredException,
            PyiCloudAuthRequiredException,
            PyiCloudFailedLoginException,
            PyiCloudServiceNotActivatedException,
        )
    except ImportError:
        print("  [跳过] 未安装 pyicloud:")
        print("         sidecar/runtime/Scripts/python.exe -m pip install pyicloud")
        return False

    sessions.mkdir(parents=True, exist_ok=True)
    print(f"  账号   : {user}")
    print(f"  口令   : {mask(password)}")
    print(f"  地区   : {'国内(云上贵州)' if china else '国际版' if china is False else '自动'}")
    print(f"  会话   : {sessions}")
    print("  (登录失败不重试, 失败即退出; 连续重跑同样会累加失败计数并触发风控锁号)")
    print()

    try:
        api = PyiCloudService(
            user,
            password,
            cookie_directory=str(sessions),
            china_mainland=china,
            pause_2fa=True,          # 不自动弹交互, 由下面显式处理
            accept_terms=True,
        )
    except PyiCloud2FARequiredException:
        print("  结论: ✗ 需要 2FA 但 pause_2fa 未生效(库行为变化), 请反馈")
        return False
    except PyiCloudFailedLoginException as e:
        print(f"  结论: ✗ 登录失败: {e}")
        print("        口令错误 / 账号被锁 / 需要 App 专用密码")
        print("        ⚠ 已停止, 不会重试。核对口令与地区后再跑**一次**, 不要连续重跑。")
        return False
    except PyiCloudServiceNotActivatedException as e:
        print(f"  结论: ✗ 该项服务未开通: {e}")
        return False
    except Exception as e:
        print(f"  结论: ✗ 登录异常 {type(e).__name__}: {str(e)[:200]}")
        return False

    # ── 2FA ──
    if getattr(api, "requires_2fa", False):
        print("  需要两步验证(2FA)。")
        try:
            if api.request_2fa_code():
                print("  ✓ 验证码已发送到你的受信任设备")
            else:
                print("  ! 发送验证码未返回成功, 仍可尝试用旧码")
        except Exception as e:
            print(f"  ! 请求验证码失败: {type(e).__name__}: {str(e)[:140]}")

        code = input("  请输入 6 位验证码: ").strip()
        try:
            ok = api.validate_2fa_code(code)
        except Exception as e:
            print(f"  结论: ✗ 验证码校验异常: {type(e).__name__}: {str(e)[:140]}")
            return False
        if not ok:
            print("  结论: ✗ 验证码错误")
            return False
        print("  ✓ 2FA 通过")

        try:
            if api.trust_session():
                print("  ✓ 已建立信任会话 —— 之后数周内这台机器不再要 2FA")
        except Exception as e:
            print(f"  ! 建立信任会话失败(不影响本次): {type(e).__name__}")
    else:
        print("  ✓ 免 2FA(已有信任会话缓存或账号未开 2FA)")

    # ── 枚举清单 ──
    try:
        svc = api.reminders
    except PyiCloudAuthRequiredException:
        print("  结论: ✗ 会话无效, 需要重新登录")
        return False
    except Exception as e:
        print(f"  结论: ✗ 取 reminders 服务失败 {type(e).__name__}: {str(e)[:200]}")
        return False

    try:
        lists = list(svc.lists())
    except Exception as e:
        print(f"  结论: ✗ 列清单失败 {type(e).__name__}: {str(e)[:200]}")
        return False

    print()
    print(f"  找到 {len(lists)} 个提醒事项清单:")
    total = 0
    for lst in lists:
        print(f"    - {lst.title}   id={lst.id[:12]}…  count={lst.count}"
              f"{'  [已删除]' if lst.deleted else ''}")
        try:
            res = svc.list_reminders(lst.id, include_completed=False, results_limit=limit)
        except Exception as e:
            print(f"        读取条目失败: {type(e).__name__}: {str(e)[:120]}")
            continue
        total += len(res.reminders)
        for r in res.reminders:
            due = f"  due={r.due_date:%Y-%m-%d %H:%M}" if r.due_date else ""
            flag = "  ⚑" if r.flagged else ""
            print(f"        · {r.title}{due}{flag}")

    try:
        cursor = svc.sync_cursor()
        print()
        print(f"  增量游标 sync_cursor() = {str(cursor)[:40]}…")
        print("  ↑ 有了它就能只拉变化项, 这正是云盘式同步的机制本身")
    except Exception as e:
        print(f"  ! 取增量游标失败(不影响读): {type(e).__name__}: {str(e)[:120]}")

    print()
    print(f"  结论: ✓ CloudKit 可用 —— {len(lists)} 个清单, 本次取样 {total} 条")
    print("        完整读写(create/update/delete) + 增量游标, 与 iOS 完全一致。")
    return True


# ======================================================================
def main() -> int:
    ap = argparse.ArgumentParser(
        description="待办(Reminders)取数探针 —— 只读",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("path", nargs="?", choices=["caldav", "cloudkit", "both"],
                    default=None)
    ap.add_argument("--user", help="Apple ID")
    # 刻意不提供 --password / --app-password: 命令行会进 shell 历史与进程列表,
    # 等于把口令写进本机日志。口令只从环境变量或 getpass 取(见文件头契约)。
    ap.add_argument("--china", action=argparse.BooleanOptionalAction, default=None,
                    help="强制国内(云上贵州)/国际版; 默认自动探测")
    ap.add_argument("--limit", type=int, default=5, help="每清单取样条数")
    ap.add_argument("--sessions", default=str(DEFAULT_SESSIONS))
    ap.add_argument("--probe-both-hosts", action="store_true",
                    help="路 B: 两个 CalDAV host 都试(默认只试探测到的一个)")
    ap.add_argument("--clear-sessions", action="store_true",
                    help="删除会话缓存(等价退出登录)后退出")
    args = ap.parse_args()

    sessions = Path(args.sessions)

    if args.clear_sessions:
        if sessions.exists():
            shutil.rmtree(sessions)
            print(f"[i] 已删除会话缓存: {sessions}")
        else:
            print(f"[i] 会话缓存不存在: {sessions}")
        return 0

    if not args.path:
        ap.error("需要指定 caldav | cloudkit | both")
    if not args.user:
        ap.error("需要 --user <Apple ID>")

    china = args.china
    if china is None:
        china = detect_china_mainland()
        print(f"[i] 地区自动探测: {'国内(云上贵州)' if china else '国际版' if china is False else '未知, 交由库判断'}")

    def get(envs: list[str], label: str) -> str:
        for e in envs:
            v = os.environ.get(e)
            if v:
                print(f"[i] {label} 取自环境变量 {e}")
                return v
        return getpass.getpass(f"{label}: ")

    ok_b = ok_a = False

    if args.path in ("caldav", "both"):
        pw = get(["ICLOUDMON_APP_PASSWORD", "ICLOUDMON_PASSWORD"], "App 专用密码")
        ok_b = probe_caldav(args.user, pw, china=china, limit=args.limit,
                            probe_both=args.probe_both_hosts)

    if args.path in ("cloudkit", "both"):
        pw = get(["ICLOUDMON_PASSWORD"], "Apple ID 密码")
        ok_a = probe_cloudkit(args.user, pw, china=china, limit=args.limit,
                              sessions=sessions)

    hr("总 结")
    print(f"  路 B CalDAV   : {'✓ 可用' if ok_b else '✗ 不可用'}")
    print(f"  路 A CloudKit : {'✓ 可用' if ok_a else '✗ 不可用'}")
    print()
    if ok_b:
        print("  → 用路 B。零 2FA、协议公开、口令可吊销, 维护成本最低。")
    elif ok_a:
        print("  → 用路 A。功能最全(含写), 注意锁 pyicloud 版本 + 保留本探针当回归。")
    else:
        print("  → 两条都不通, 先看上面的错误再决定。")
    print()
    return 0 if (ok_b or ok_a) else 1


if __name__ == "__main__":
    sys.exit(main())
