# ======================================================================
#  stall.py  -  活动指纹 + 历史落盘 + 卡死判定
#               (对应 IcloudMonitor::fingerprint / appendHistory / detectStall)
#
#  规则与 C++ 版逐条对齐:
#    指纹绝不能含时间戳/速率, 否则每帧都变, 停滞永远检测不出来;
#    空闲(非 busy) 不算卡死;
#    若观测期内指纹从未变化, last_change 退化为观测起点 —— 否则停滞时长
#    恒等于"距上次采样间隔", 永远小于阈值, 卡死检测形同虚设。
# ======================================================================
from __future__ import annotations

import json
import time
from dataclasses import dataclass
from pathlib import Path

from . import paths
from .snapshot import Snapshot

HISTORY_MAX = 2000


def fingerprint(s: Snapshot) -> dict:
    q = s.queue
    fp = {
        "need_up_n": q.need_up_count,
        "need_up_b": q.need_up_bytes,
        "need_dl_n": q.need_dl_count,
        "need_dl_b": q.need_dl_bytes,
        "pending_n": q.pending_fetches,
        "unstaged": q.dl_unstaged,
        "synced_up": q.synced_up_count,
        "downloaded": q.downloaded_count,
        "uploaded_b": q.uploaded_bytes,
        "downloaded_b": q.downloaded_bytes,
        "faults": s.faults,
        "over_quota": s.over_quota,
        "rank": s.rank_client,
        "unapplied": s.unapplied,
        "local_pending": sorted(
            ({"n": p.name, "b": p.bytes} for p in s.local_pendings),
            key=lambda d: (d["n"], d["b"])),
    }
    return fp


def append_history(s: Snapshot) -> int:
    """追加一条历史记录, 超上限裁头。返回当前条数。"""
    path = Path(paths.state_file())
    now = time.time()
    rec = {
        "ts": time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(now)),
        "epoch": now,
        "busy": s.is_busy(),
        "state": s.state_name(),
        "fp": fingerprint(s),
    }
    with path.open("a", encoding="utf-8") as f:
        f.write(json.dumps(rec, ensure_ascii=False, separators=(",", ":")) + "\n")

    lines = [ln for ln in path.read_text(encoding="utf-8").splitlines() if ln.strip()]
    if len(lines) > HISTORY_MAX:
        kept = lines[-HISTORY_MAX:]
        path.write_text("\n".join(kept) + "\n", encoding="utf-8")
        return len(kept)
    return len(lines)


@dataclass
class StallInfo:
    stalled: bool = False
    seconds: int = 0
    observed: int = 0
    detail: str = ""


def detect_stall(stall_seconds: int, state_file: str | None = None) -> StallInfo:
    info = StallInfo()
    path = Path(state_file or paths.state_file())
    if not path.exists():
        return info

    hist: list[dict] = []
    for ln in path.read_text(encoding="utf-8").splitlines():
        ln = ln.strip()
        if not ln:
            continue
        try:
            obj = json.loads(ln)
        except json.JSONDecodeError:
            continue
        if isinstance(obj, dict):
            hist.append(obj)
    if not hist:
        return info

    cur = hist[-1]
    if not cur.get("busy"):
        info.detail = "空闲中(无任务), 不构成卡死"
        return info

    cur_fp = cur.get("fp") or {}
    first_epoch = float(hist[0].get("epoch") or 0)
    last_change = -1.0
    for rec in reversed(hist[:-1]):
        if (rec.get("fp") or {}) != cur_fp:
            last_change = float(rec.get("epoch") or 0)
            break
    note = ""
    if last_change < 0:
        last_change = first_epoch
        note = "(观测期内指纹从未变化)"

    now = time.time()
    info.seconds = int(now - last_change)
    info.observed = int(now - first_epoch)
    info.stalled = info.seconds > stall_seconds

    bits: list[str] = []
    up_n = int(cur_fp.get("need_up_n") or 0)
    dl_n = int(cur_fp.get("need_dl_n") or 0)
    pf = int(cur_fp.get("pending_n") or 0)
    if up_n:
        bits.append(f"待上传 {up_n} 个")
    if dl_n:
        bits.append(f"待下载 {dl_n} 个")
    if pf:
        bits.append(f"取回队列 {pf} 条")
    lp = cur_fp.get("local_pending") or []
    if lp:
        names = ", ".join(str(x.get("n")) for x in lp[:3])
        bits.append(f"本地未提交 {len(lp)} 项 ({names})")
    faults = int(cur_fp.get("faults") or 0)
    if faults > 0:
        bits.append(f"故障 {faults}")
    body = "; ".join(bits) if bits else "活动状态但无明确队列项"
    info.detail = f"{body} {note}".rstrip()
    return info


def reset_history() -> None:
    p = Path(paths.state_file())
    if p.exists():
        p.unlink()
