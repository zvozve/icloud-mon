# ======================================================================
#  bridge.py  -  Snapshot -> QML 可直接绑定的扁平 JSON
#                (对应 Bridge.h 的属性面 + Bridge::onAnomaly 的异常语义)
#
#  ★ 字段名刻意与 Bridge.h 的 Q_PROPERTY 同名: QML 侧换掉数据源后
#    绑定表达式无需改动, 这是"复刻当前功能"能做到最小改动的前提。
# ======================================================================
from __future__ import annotations

import time

from . import paths
from .procs import running_processes
from .snapshot import Snapshot, human_size
from .stall import StallInfo


def _anomalies(s: Snapshot, st: StallInfo, stall_seconds: int) -> list[dict]:
    """异常判定与顺序完全对齐 IcloudMonitor::doSample。
    最后一条会覆盖 anomalyTitle/anomalyDetail (C++ 里也是后来者覆盖)。"""
    items: list[dict] = []
    if not s.db_ok:
        items.append({"kind": "严重", "title": "数据库不可读",
                      "detail": s.db_error})
        return items
    if st.stalled:
        items.append({
            "kind": "卡死",
            "title": f"活动状态停滞 {st.seconds} 秒 (阈值 {stall_seconds} 秒)",
            "detail": st.detail})
    if s.faults > 0:
        items.append({"kind": "错误", "title": f"故障项 {s.faults} 个",
                      "detail": "详见错误面板"})
    if s.over_quota > 0:
        items.append({"kind": "配额", "title": f"超配额 {s.over_quota} 个",
                      "detail": "iCloud 云空间已满"})
    if s.upload_errors:
        items.append({"kind": "上传错误", "title": "存在上传错误码",
                      "detail": ""})
    if s.download_errors:
        items.append({"kind": "下载错误", "title": "存在下载错误码",
                      "detail": ""})
    return items


def _last_error(s: Snapshot) -> str:
    if not s.db_ok:
        return s.db_error
    if s.faults > 0:
        return f"故障项 {s.faults} 个"
    if s.upload_errors:
        l = ", ".join(f"码{c} x{n}" for c, n in s.upload_errors)
        return f"上传错误 {l}"
    if s.download_errors:
        l = ", ".join(f"码{c} x{n}" for c, n in s.download_errors)
        return f"下载错误 {l}"
    return ""


def _rank_text(s: Snapshot) -> str:
    if not s.has_rank:
        return "游标不可读"
    tail = "已追平" if s.in_sync else "追赶中"
    return f"client={s.rank_client}  server={s.rank_server}  待应用={s.unapplied}  -> {tail}"


def build(s: Snapshot, st: StallInfo, *, running: list[str] | None = None,
          refresh_sec: int = 5, stall_sec: int = 300,
          close_to_tray: bool = False, restarting: bool = False) -> dict:
    q = s.queue
    # upCount 取"队列计数"与"本地未提交项"的较大值 —— 见 Bridge.h 的长注释:
    # needs_upload_count 实测长期为 0/滞后, 真正实时的是本地未提交项。
    local_up = sum(1 for p in s.local_pendings
                   if p.sync_up_state or p.in_flight or p.has_diff)
    up_count = max(q.need_up_count, local_up)

    proc_list = running if running is not None else running_processes()
    anomalies = _anomalies(s, st, stall_sec)

    payload = {
        # 主状态
        "stateName": s.state_name(),
        "stateDetail": s.state_detail(),
        "busy": s.is_busy(),
        "inSync": s.in_sync,
        "dbOk": s.db_ok,
        "clientVersion": s.client_version,
        "timestamp": time.strftime("%H:%M:%S"),
        "elapsedMs": s.elapsed_ms,

        # 上传
        "upDone": q.synced_up_count,
        "upTotal": q.synced_up_count + q.need_up_count,
        "upCount": up_count,
        "upSize": human_size(q.need_up_bytes),
        "upBusy": up_count > 0,

        # 下载
        "dlDone": q.downloaded_count,
        "dlTotal": q.downloaded_count + q.need_dl_count,
        "dlCount": q.need_dl_count,
        "dlSize": human_size(q.need_dl_bytes),
        "dlPending": q.pending_fetches,
        "dlBusy": q.need_dl_count > 0 or q.pending_fetches > 0,

        # 删除: 待删除 = needs_delete_doc_count; 已完成 = 回收站项数
        "delDone": s.trash_count,
        "delTotal": s.trash_count + q.need_del_count,
        "delCount": q.need_del_count,
        "delBusy": q.need_del_count > 0,

        # 错误
        "faults": s.faults,
        "overQuota": s.over_quota,
        "lastError": _last_error(s),
        "rankText": _rank_text(s),

        # 待处理分类 (仅云端不算任务; 孤立记录不算待处理)
        "cloudOnlyCount": s.cloud_only_count,
        "pendingRealCount": s.pending_dir_count + sum(
            1 for it in s.pending_items if not it.missing),
        "pendingOrphanCount": sum(1 for it in s.pending_items if it.missing),
        "localPendingTotal": s.local_pending_total,

        # 云端状态分布
        "pcsBuckets": [
            {"pcsState": b.pcs_state, "count": b.count,
             "bytes": b.bytes, "label": b.label}
            for b in s.pcs_dist
        ],

        # 明细列表 (QML 的三个 ListModel 直接用数组填充)
        "pendingItems": [
            {"name": it.name, "size": it.bytes, "isDir": it.is_dir,
             "sizeText": human_size(it.bytes), "path": it.path,
             "missing": it.missing}
            for it in s.pending_items
        ],
        "localPendings": [
            {"name": p.name, "bytes": p.bytes, "path": p.abs_path,
             "stateText": ", ".join(
                 x for x in (
                     f"syncup={p.sync_up_state}" if p.sync_up_state else "",
                     "inflight" if p.in_flight else "",
                     "diff" if p.has_diff else "") if x)}
            for p in s.local_pendings
        ],
        "trashItems": [
            {"name": t.name, "restorePath": t.restore_path}
            for t in s.trash_items
        ],

        # zone (信息展示)
        "zoneTotal": s.zone_total,
        "activeZones": [
            {"name": z.name, "syncState": z.sync_state,
             "requestId": z.request_id} for z in s.active_zones
        ],

        # ── 原始计数字段 ──
        # 上面那些是"界面要的成品"(已做过取大值/换文案等加工); 这一组是**未经
        # 加工的队列原值**, 供 C++ 侧把 payload 反填回 Snapshot 用 —— 目前只有
        # 四类同步分类(IcloudCategories, 尚未迁到 Python)需要, 它按 Snapshot
        # 取值。分类迁完后这组字段可以删。
        "hasRank": s.has_rank,
        "rankClient": s.rank_client,
        "rankServer": s.rank_server,
        "unapplied": s.unapplied,
        "needUpCount": q.need_up_count,
        "needUpBytes": q.need_up_bytes,
        "needDlCount": q.need_dl_count,
        "needDlBytes": q.need_dl_bytes,
        "needDelCount": q.need_del_count,
        "pendingFetches": q.pending_fetches,
        "dlUnstaged": q.dl_unstaged,
        "trashCount": s.trash_count,

        # iCloud 进程
        "icloudRunning": bool(proc_list),
        "icloudProcesses": ", ".join(proc_list),
        "busyRestarting": restarting,

        # 设置
        "driveRoot": paths.drive_root(),
        "refreshSec": refresh_sec,
        "stallSec": stall_sec,
        "closeToTray": close_to_tray,

        # 异常: 明细列表 + 沿用 C++ 的"最后一条覆盖标题/详情"语义
        "anomalous": bool(anomalies),
        "anomalyTitle": anomalies[-1]["title"] if anomalies else "",
        "anomalyDetail": anomalies[-1]["detail"] if anomalies else "",
        "anomalyList": [a["kind"] for a in anomalies],
        "anomalies": anomalies,
    }
    return payload
