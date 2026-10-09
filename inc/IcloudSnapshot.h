// ======================================================================
//  IcloudSnapshot.h  -  一次采样的完整数据快照
//
//  所有字段都来自 iCloudDrive.db 的真实计数, 不是估算。
//  语义经过实测确认 (见 README):
//    need_up_n / need_dl_n  空队列时为 0, 非 0 表示确实在传
//    item_state              0=已同步, 非 0=待处理
//    pcs_state               0=空文件 1=有本地实体 2=仅云端 4=目录
//    item_localsyncupstate   0=干净, 非 0(含 4)=卡在进行中
// ======================================================================
#ifndef ICLOUDSNAPSHOT_H
#define ICLOUDSNAPSHOT_H

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace icloud {

// ── 上传/下载队列计数 ──────────────────────────────────────────────
struct QueueStat
{
    int     needUpCount   = 0;   // 待上传个数
    qint64  needUpBytes   = 0;   // 待上传字节
    int     needDlCount   = 0;   // 待下载个数
    qint64  needDlBytes   = 0;   // 待下载字节
    int     pendingFetches = 0;  // server_items_pending_fetches 行数
    int     dlUnstaged    = 0;   // 下载暂存中
    int     needDlUnstage = 0;   // 待暂存
    int     syncedUpCount = 0;   // 已完成上传
    int     uploadedCount = 0;   // 已上传
    qint64  uploadedBytes = 0;
    int     downloadedCount = 0; // 已完成下载
    qint64  downloadedBytes = 0;
    int     needSyncUp    = 0;

    bool uploadIdle() const { return needUpCount == 0; }
    bool downloadIdle() const { return needDlCount == 0 && pendingFetches == 0; }
};

// ── 云端文件状态分布 (server_items.pcs_state) ──────────────────────
struct PcsBucket
{
    int    pcsState = 0;
    int    count    = 0;
    qint64 bytes    = 0;
    QString label;   // 中文标签
};

// ── 待处理项 ───────────────────────────────────────────────────────
struct PendingItem
{
    QString name;
    qint64  bytes = 0;
    bool    isDir = false;
};

// ── 本地项未提交变更 (local_items 有 diff / syncup 非 0) ──────────
struct LocalPending
{
    QString name;
    QString absPath;
    qint64  bytes      = 0;
    int     syncUpState = 0;
    bool    inFlight   = false;
    bool    hasDiff    = false;
};

// ── Zone 状态 ──────────────────────────────────────────────────────
struct ZoneInfo
{
    QString name;
    int     syncState = 0;
    QString requestId;
};

// ── 完整快照 ───────────────────────────────────────────────────────
struct Snapshot
{
    bool    dbOk     = false;   // 数据库是否可读
    QString dbError;            // 不可读时的原因
    QString clientVersion;

    // 同步游标: client == server 且 unapplied == 0 => 真正追平
    bool    hasRank   = false;
    qint64  rankClient = 0;
    qint64  rankServer = 0;
    int     unapplied  = 0;
    bool    inSync     = false;

    QueueStat      queue;
    QVector<PcsBucket>    pcsDist;
    QVector<PendingItem>  pendingItems;
    int                    pendingDirCount = 0;
    QVector<LocalPending> localPendings;
    QVector<ZoneInfo>     activeZones;
    int                    zoneTotal = 0;

    // 错误
    int     faults      = 0;
    int     overQuota   = 0;
    QVector<QPair<int, int>> uploadErrors;    // (错误码, 个数)
    QVector<QPair<int, int>> downloadErrors;

    qint64  elapsedMs = 0;

    // ── 派生状态 ────────────────────────────────────────────────
    // 空闲 / 同步中 / 上传中 / 下载中 / 双向传输 / 暂存中 / 上传处理中
    QString stateName() const;
    QString stateDetail() const;
    bool    isBusy() const;
};

}  // namespace icloud

#endif  // ICLOUDSNAPSHOT_H
