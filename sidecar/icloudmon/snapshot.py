# ======================================================================
#  snapshot.py  -  一次采样 (对应 inc/IcloudSnapshot.h + IcloudMonitor 的
#                  stageDatabase / readSnapshot)
#
#  原则: 数值全部来自 iCloudDrive.db 的真实计数, 不做任何估算。
#  字段语义与 C++ 版逐条对齐 (见 IcloudSnapshot.h 顶部注释), 不得自行解释。
# ======================================================================
from __future__ import annotations

import os
import shutil
import sqlite3
import tempfile
import time
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path

from . import paths

# 面板高度固定, 明细列表只取前若干条; 真实总数另行 COUNT
PENDING_SHOWN = 20
LOCAL_PENDING_SHOWN = 6
TRASH_SHOWN = 30


def human_size(b: int) -> str:
    units = ["B", "KB", "MB", "GB", "TB"]
    v = float(b)
    i = 0
    while v >= 1024.0 and i < 4:
        v /= 1024.0
        i += 1
    return f"{v:.1f} {units[i]}"


@dataclass
class QueueStat:
    need_up_count: int = 0
    need_up_bytes: int = 0
    need_dl_count: int = 0
    need_dl_bytes: int = 0
    pending_fetches: int = 0
    dl_unstaged: int = 0
    need_dl_unstage: int = 0
    synced_up_count: int = 0
    uploaded_count: int = 0
    uploaded_bytes: int = 0
    downloaded_count: int = 0
    downloaded_bytes: int = 0
    need_sync_up: int = 0
    need_del_count: int = 0


@dataclass
class PcsBucket:
    pcs_state: int = 0
    count: int = 0
    bytes: int = 0
    label: str = ""


@dataclass
class PendingItem:
    name: str = ""
    bytes: int = 0
    is_dir: bool = False
    path: str = ""
    missing: bool = False


@dataclass
class LocalPending:
    name: str = ""
    abs_path: str = ""
    bytes: int = 0
    sync_up_state: int = 0
    in_flight: bool = False
    has_diff: bool = False


@dataclass
class TrashItem:
    name: str = ""
    restore_path: str = ""


@dataclass
class ZoneInfo:
    name: str = ""
    sync_state: int = 0
    request_id: str = ""


@dataclass
class Snapshot:
    db_ok: bool = False
    db_error: str = ""
    client_version: str = ""

    has_rank: bool = False
    rank_client: int = 0
    rank_server: int = 0
    unapplied: int = 0
    in_sync: bool = False

    queue: QueueStat = field(default_factory=QueueStat)
    pcs_dist: list[PcsBucket] = field(default_factory=list)
    pending_items: list[PendingItem] = field(default_factory=list)
    pending_dir_count: int = 0
    cloud_only_count: int = 0
    local_pendings: list[LocalPending] = field(default_factory=list)
    local_pending_total: int = 0
    trash_items: list[TrashItem] = field(default_factory=list)
    trash_count: int = 0
    active_zones: list[ZoneInfo] = field(default_factory=list)
    zone_total: int = 0

    faults: int = 0
    over_quota: int = 0
    upload_errors: list[tuple[int, int]] = field(default_factory=list)
    download_errors: list[tuple[int, int]] = field(default_factory=list)

    elapsed_ms: int = 0

    # ── 派生状态 (判据与 C++ 完全一致, 别自行加条件) ────────────────
    def state_name(self) -> str:
        if not self.db_ok:
            return "未知"
        q = self.queue
        if q.need_up_count > 0 and (q.need_dl_count > 0 or q.pending_fetches > 0):
            return "双向传输"
        if q.need_up_count > 0:
            return "上传中"
        if q.need_dl_count > 0 or q.pending_fetches > 0:
            return "下载中"
        if q.dl_unstaged > 0:
            return "暂存中"
        if q.need_del_count > 0:
            return "删除中"
        # ★ 队列空闲时只看同步游标。绝不能拿 local_pendings / active_zones
        #   当"还在忙"的判据 —— 前者长期挂着历史行, 后者 currentrequestid
        #   是 iCloud 常驻注册的, 用了会让状态永远停在"处理中"。
        if not self.in_sync:
            return "同步中"
        if self.local_pendings:
            return "待提交"
        return "已同步"

    def state_detail(self) -> str:
        if not self.db_ok:
            return self.db_error
        q = self.queue
        if q.need_up_count > 0 and (q.need_dl_count > 0 or q.pending_fetches > 0):
            return f"待上传 {q.need_up_count} + 待下载 {q.need_dl_count}"
        if q.need_up_count > 0:
            return f"{q.need_up_count} 个待上传 {human_size(q.need_up_bytes)}"
        if q.need_dl_count > 0 or q.pending_fetches > 0:
            return f"{q.need_dl_count} 个待下载 {human_size(q.need_dl_bytes)}"
        if q.dl_unstaged > 0:
            return f"{q.dl_unstaged} 个下载暂存"
        if q.need_del_count > 0:
            return f"{q.need_del_count} 个待删除"
        if not self.in_sync:
            return f"正在同步 · 待应用 {self.unapplied} 项"
        if self.local_pendings:
            return f"{len(self.local_pendings)} 个本地项待提交"
        return "全部已同步"

    def is_busy(self) -> bool:
        if not self.db_ok:
            return False
        q = self.queue
        return bool(q.need_up_count or q.need_dl_count or q.pending_fetches
                    or q.dl_unstaged or q.need_del_count or self.local_pendings)


# ── 数据库 staging: 复制 db + wal + shm 到临时目录 ──────────────────
_STAGE_DIR: str | None = None


def _stage_dir() -> str:
    """进程级临时目录, 随进程退出清理 —— 与 C++ stageDatabase 一致。"""
    global _STAGE_DIR
    if _STAGE_DIR is None:
        _STAGE_DIR = tempfile.mkdtemp(prefix="icloudmon-")
    return _STAGE_DIR


def stage_database() -> tuple[str, str]:
    """返回 (临时库路径, 错误)。三个文件必须一起复制, 少一个队列就读不到。"""
    src = paths.db_file()
    if not Path(src).exists():
        return "", f"数据库不存在: {src}"

    dst = Path(_stage_dir()) / "iCloudDrive.db"
    for suffix in ("", "-wal", "-shm"):
        s = Path(src + suffix)
        d = Path(str(dst) + suffix)
        if not s.exists():
            continue
        if d.exists():
            d.unlink()
        shutil.copy2(s, d)
    return str(dst), ""


# ── 同步根目录解析 ──────────────────────────────────────────────────
#
#  iCloud 把同步根改成别处时(用户自定义位置), 程序必须能自己找出来 ——
#  写死任何一个路径都会"换台机器就指空"。
#
#  权威源: iCloudDrive.db 的 local_items.item_absolutepath, 存的是 iCloud
#  自己写下的**完整本地绝对路径**(机器无关)。但表里没有"根"那一行,
#  需要从已知项反推:
#
#    从任一已知项路径向上逐级取祖先, 选**含有 .Trash 子目录**的最高一层。
#    (嵌套 .Trash 存在 —— 如 <root>\iCloud~md~obsidian\.Trash, 取最高即消歧。)
#
#  实测(本机 200 条样本): 推出唯一解且正确 = C:\Container\Cloud\iCloudDrive。
#
#  ⚠ 全过程只读: 只是 is_dir() 探测 + 只读打开数据库, 绝不新建/移动/改名
#    同步根目录里的任何东西。
_derived_root_cache: str | None = None


def discover_drive_root(db_path: str | None) -> str | None:
    """从(已 staging 的)数据库反推同步根目录。找不到返回 None。只读。"""
    if not db_path or not Path(db_path).exists():
        return None
    try:
        conn = sqlite3.connect(f"file:{Path(db_path).as_posix()}?mode=ro", uri=True)
        try:
            rows = conn.execute(
                "SELECT item_absolutepath FROM local_items "
                "WHERE COALESCE(item_absolutepath,'')<>'' LIMIT 500").fetchall()
        finally:
            conn.close()
    except sqlite3.Error:
        return None

    hits: list[str] = []
    for row in rows:
        raw = row[0]
        p = raw.decode("utf-8", "ignore") if isinstance(raw, (bytes, bytearray)) else str(raw)
        parts = p.split("\\")
        # ★ 必须**从盘根往深处**逐级判断, 取第一个"含 .Trash"的目录。
        #   反过来(从深往浅)会先撞上嵌套的 .Trash —— 例如
        #   <root>\iCloud~md~obsidian\.Trash —— 于是把子目录当成了同步根
        #   (实测踩过: 推出 …\iCloudDrive\iCloud~md~obsidian\ZOZE-A)。
        for i in range(1, len(parts) + 1):
            d = "\\".join(parts[:i])
            if len(d) <= 3:          # "C:" 这种盘根跳过
                continue
            if Path(d, ".Trash").is_dir():
                hits.append(d)
                break

    if not hits:
        return None
    # 所有样本应指向同一个根; 取出现次数最多者(个别异常路径不影响结果)
    root = Counter(hits).most_common(1)[0][0]
    return root.replace("\\", "/")


def current_drive_root(db_path: str | None = None) -> str:
    """同步根目录的唯一解析入口。

    优先级: 环境变量 > 注册表(用户显式覆盖) > 数据库推导 > %USERPROFILE%\\iCloudDrive

    · ①② 每次现读(便宜, 且用户改了设置立刻生效);
    · ③ 推导要读库, 结果缓存(昂贵, 且同步根极少变)。
    """
    env = os.environ.get("ICLOUD_DRIVE_ROOT")
    if env:
        return env.replace("\\", "/")

    saved = paths.setting_get("driveRoot")
    if saved:
        return str(saved).replace("\\", "/")

    global _derived_root_cache
    if _derived_root_cache is None:
        _derived_root_cache = (discover_drive_root(db_path)
                               or paths.drive_root_fallback())
    return _derived_root_cache


# ── 读快照 ──────────────────────────────────────────────────────────
def read_snapshot(db_path: str, drive_root: str) -> Snapshot:
    out = Snapshot()
    conn = sqlite3.connect(f"file:{Path(db_path).as_posix()}?mode=ro", uri=True)
    try:
        cur = conn.cursor()

        def q(sql: str):
            cur.execute(sql)
            return cur.fetchall()

        # 1) 客户端版本
        for row in q("SELECT version FROM app_info LIMIT 1"):
            out.client_version = str(row[0])

        # 2) 同步游标 (client == server 且 unapplied == 0 才算追平)
        try:
            c_rank = q("SELECT next_syncdown_rank FROM client_state LIMIT 1")
            s_rank = q("SELECT next_syncdown_rank FROM server_state LIMIT 1")
            out.unapplied = int(q("SELECT COUNT(*) FROM unapplied_ranks")[0][0])
            if c_rank and s_rank:
                out.has_rank = True
                out.rank_client = int(c_rank[0][0])
                out.rank_server = int(s_rank[0][0])
                out.in_sync = (out.rank_client == out.rank_server
                               and out.unapplied == 0)
        except sqlite3.Error:
            pass

        # 3) 队列计数
        rows = q(
            "SELECT COALESCE(SUM(needs_upload_count),0),"
            "       COALESCE(SUM(needs_upload_size),0),"
            "       COALESCE(SUM(needs_download_count),0),"
            "       COALESCE(SUM(needs_download_size),0),"
            "       COALESCE(SUM(synced_up_count),0),"
            "       COALESCE(SUM(uploaded_count),0),"
            "       COALESCE(SUM(uploaded_size),0),"
            "       COALESCE(SUM(downloaded_count),0),"
            "       COALESCE(SUM(downloaded_size),0),"
            "       COALESCE(SUM(dl_unstaged_count),0),"
            "       COALESCE(SUM(needs_dl_unstage_count),0),"
            "       COALESCE(SUM(faults_count),0),"
            "       COALESCE(SUM(over_quota_count),0),"
            "       COALESCE(SUM(needs_sync_up_count),0),"
            "       COALESCE(SUM(needs_delete_doc_count),0) "
            "FROM item_recursive_properties")
        if rows:
            r = rows[0]
            s = out.queue
            s.need_up_count = int(r[0])
            s.need_up_bytes = int(r[1])
            s.need_dl_count = int(r[2])
            s.need_dl_bytes = int(r[3])
            s.synced_up_count = int(r[4])
            s.uploaded_count = int(r[5])
            s.uploaded_bytes = int(r[6])
            s.downloaded_count = int(r[7])
            s.downloaded_bytes = int(r[8])
            s.dl_unstaged = int(r[9])
            s.need_dl_unstage = int(r[10])
            out.faults = int(r[11])
            out.over_quota = int(r[12])
            s.need_sync_up = int(r[13])
            s.need_del_count = int(r[14])

        # 4) 待取回队列
        out.queue.pending_fetches = int(
            q("SELECT COUNT(*) FROM server_items_pending_fetches")[0][0])

        # 5) 云端状态分布
        labels = {0: "空文件", 1: "有本地实体", 2: "仅云端", 4: "目录"}
        for state, cnt, size in q(
                "SELECT pcs_state, COUNT(*), COALESCE(SUM(version_size),0) "
                "FROM server_items GROUP BY pcs_state ORDER BY 2 DESC"):
            st = int(state)
            out.pcs_dist.append(PcsBucket(
                pcs_state=st, count=int(cnt), bytes=int(size),
                label=labels.get(st, f"未知({st})")))

        # 6) 待处理项明细
        # ★ 必须排除 pcs_state=2(仅云端): 那是按需下载的正常状态, 不是任务。
        for name, itype, size, rel in q(
                "SELECT item_filename, item_type, COALESCE(version_size,0), "
                "       COALESCE(item_restore_path,'') "
                "FROM server_items WHERE item_state<>0 AND pcs_state<>2 "
                f"ORDER BY version_size DESC LIMIT {PENDING_SHOWN}"):
            it = PendingItem(name=str(name), is_dir=(int(itype) == 0),
                             bytes=int(size))
            rel = str(rel or "")
            if rel:
                it.path = f"{drive_root.rstrip('/')}/{rel}".replace("\\", "/")
                # 文件已不在磁盘 = 孤立记录(状态没清干净), 不是真在排队
                it.missing = not Path(it.path).exists()
            out.pending_items.append(it)

        out.cloud_only_count = int(
            q("SELECT COUNT(*) FROM server_items WHERE pcs_state=2")[0][0])
        out.pending_dir_count = int(q(
            "SELECT COUNT(*) FROM server_items "
            "WHERE item_state<>0 AND item_type=0 AND pcs_state<>2")[0][0])

        # 7) 本地未提交项 (卡死判定的关键信号)
        out.local_pending_total = int(q(
            "SELECT COUNT(*) FROM local_items "
            "WHERE item_localsyncupstate<>0 "
            "   OR item_in_flight_diffs<>0 OR item_local_diffs<>0")[0][0])
        for name, _t, sync_up, size, inflight, diffs, abspath in q(
                "SELECT item_filename, item_type, item_localsyncupstate, "
                "       COALESCE(version_size,0), COALESCE(item_in_flight_diffs,0), "
                "       COALESCE(item_local_diffs,0), item_absolutepath "
                "FROM local_items WHERE item_localsyncupstate<>0 "
                "   OR item_in_flight_diffs<>0 OR item_local_diffs<>0 "
                f"LIMIT {LOCAL_PENDING_SHOWN}"):
            out.local_pendings.append(LocalPending(
                name=str(name), abs_path=str(abspath or ""), bytes=int(size),
                sync_up_state=int(sync_up),
                in_flight=bool(int(inflight)), has_diff=bool(int(diffs))))

        # 7.5) 回收站 (删除队列的"已完成"侧)
        for name, restore in q(
                "SELECT item_filename, COALESCE(item_restore_path,'') "
                "FROM local_items WHERE COALESCE(item_restore_path,'')<>'' "
                f"ORDER BY item_filename LIMIT {TRASH_SHOWN}"):
            out.trash_items.append(TrashItem(name=str(name),
                                             restore_path=str(restore)))
        out.trash_count = int(q(
            "SELECT COUNT(*) FROM local_items "
            "WHERE COALESCE(item_restore_path,'')<>''")[0][0])

        # 8) 活跃 zone (仅信息展示, 不参与忙/闲判定)
        for name, state, reqid in q(
                "SELECT zonename, sync_state, currentrequestid "
                "FROM server_zones WHERE currentrequestid <> ''"):
            out.active_zones.append(ZoneInfo(name=str(name),
                                             sync_state=int(state),
                                             request_id=str(reqid)))
        out.zone_total = int(q("SELECT COUNT(*) FROM server_zones")[0][0])

        # 9) 错误码
        for code, cnt in q(
                "SELECT upload_error_code, COUNT(*) FROM local_items "
                "WHERE upload_error_code <> 0 GROUP BY upload_error_code"):
            out.upload_errors.append((int(code), int(cnt)))
        for code, cnt in q(
                "SELECT download_error_code, COUNT(*) FROM local_items "
                "WHERE download_error_code <> 0 GROUP BY download_error_code"):
            out.download_errors.append((int(code), int(cnt)))

    finally:
        conn.close()
    return out


def sample() -> Snapshot:
    """一次完整采样: 复制三件套 + 读库 (与 C++ doSample 的前半段等价)。"""
    t0 = time.perf_counter()
    db_path, err = stage_database()
    if not db_path:
        s = Snapshot(db_ok=False, db_error=err,
                     elapsed_ms=int((time.perf_counter() - t0) * 1000))
        return s
    try:
        # 根目录解析放在这里: 已经 staging 好 db, 推导不再多复制一份。
        s = read_snapshot(db_path, current_drive_root(db_path))
        s.db_ok = True
    except sqlite3.Error as e:
        s = Snapshot(db_ok=False, db_error=f"读库失败: {e}")
    s.elapsed_ms = int((time.perf_counter() - t0) * 1000)
    return s
