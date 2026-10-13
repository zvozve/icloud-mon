// ======================================================================
//  IcloudSnapshot.cpp  -  Snapshot / QueueStat 上"由数据推导"的那几个方法
//
//  ★ 为什么单独一个文件:
//    这三个方法(stateName / stateDetail / isBusy)原来住在 IcloudMonitor.cpp
//    里 —— 那只是历史摆放位置, 不是依赖关系。它们只吃 Snapshot 自己的字段,
//    不碰数据库、不碰 QObject、不发信号。
//
//    数据层切到 Python 旁路后 IcloudMonitor.cpp 下线了, 而 IcloudCategories
//    (四类同步卡片)仍要用 Snapshot 判状态, 于是链接期缺符号。把纯派生逻辑
//    摘出来放这里, 两边都能用, 也顺带确认了"Snapshot 是自包含的数据结构"。
//
//    注意: 判据要与 Python 侧 icloudmon/snapshot.py 的 state_name/state_detail
//    严格同构 —— 同一份数据两条路径给出不同结论是最难查的 bug。
// ======================================================================

#include "IcloudSnapshot.h"

#include <QLatin1String>
#include <QString>

namespace icloud {

QString Snapshot::stateName() const
{
    if (!dbOk)
        return QStringLiteral("未知");

    const QueueStat &q = queue;
    if (q.needUpCount > 0 && (q.needDlCount > 0 || q.pendingFetches > 0))
        return QStringLiteral("双向传输");
    if (q.needUpCount > 0)
        return QStringLiteral("上传中");
    if (q.needDlCount > 0 || q.pendingFetches > 0)
        return QStringLiteral("下载中");
    if (q.dlUnstaged > 0)
        return QStringLiteral("暂存中");
    if (q.needDelCount > 0)
        return QStringLiteral("删除中");

    // ── 队列空闲: 到底同步完没有, 用**同步游标**判定 ──
    //
    // ★ 这里原来还有两条"非队列"的忙碌判据, 会让状态永远停在"处理中":
    //     1) !localPendings.isEmpty() -> "上传处理中"
    //     2) !activeZones.isEmpty()   -> "同步中"
    //   实测两条都不成立:
    //     - localPendings 取 local_items 里 item_localsyncupstate<>0 /
    //       item_in_flight_diffs<>0 / item_local_diffs<>0 的行。这类行
    //       只要存在就长期命中, 即使 needs_upload_count=0、一个字节都没在传。
    //       (实测某次同步早已完成, 仍有 3 个 .md/.js 长期挂着, 状态却一直
    //        显示"上传处理中"。)
    //     - activeZones 取 server_zones.currentrequestid<>''。而
    //       com.apple.CloudDocs / iCloud.md.obsidian 的 currentrequestid
    //       是 iCloud **常驻注册**的请求 id, 常态非空, 同步完成后也不清 ——
    //       所以"同步中"是常态, 等于这个状态从来没提供有效信息。
    //
    //   "忙"只认队列(真在传东西)。队列空了就用游标判是否追平:
    //   client==server 且无待应用 rank 才是真正的"同步完成"。
    if (!inSync)
        return QStringLiteral("同步中");
    // 游标已追平但本地还有改动没被 iCloud 接收 —— 这是"待提交",
    // 不是"正在处理"。措辞上区分开, 免得让人以为还在传。
    if (!localPendings.isEmpty())
        return QStringLiteral("待提交");
    return QStringLiteral("已同步");
}

QString Snapshot::stateDetail() const
{
    if (!dbOk)
        return dbError;

    const QueueStat &q = queue;
    auto human = [](qint64 b) {
        const char *u[] = { "B", "KB", "MB", "GB", "TB" };
        double v = double(b);
        int i = 0;
        while (v >= 1024.0 && i < 4) { v /= 1024.0; ++i; }
        return QStringLiteral("%1 %2").arg(v, 0, 'f', 1).arg(QLatin1String(u[i]));
    };

    if (q.needUpCount > 0 && (q.needDlCount > 0 || q.pendingFetches > 0))
        return QStringLiteral("待上传 %1 + 待下载 %2")
            .arg(q.needUpCount).arg(q.needDlCount);
    if (q.needUpCount > 0)
        return QStringLiteral("%1 个待上传 %2")
            .arg(q.needUpCount).arg(human(q.needUpBytes));
    if (q.needDlCount > 0 || q.pendingFetches > 0)
        return QStringLiteral("%1 个待下载 %2")
            .arg(q.needDlCount).arg(human(q.needDlBytes));
    if (q.dlUnstaged > 0)
        return QStringLiteral("%1 个下载暂存").arg(q.dlUnstaged);
    if (q.needDelCount > 0)
        return QStringLiteral("%1 个待删除").arg(q.needDelCount);
    // 与 stateName 同一套判据: 队列空 -> 看游标, 不看 activeZones
    // (activeZones 常态非空, 拿它当"还在忙"会让这句永远显示
    //  "2 个 zone 有活跃请求", 同步早就完成了也照报。)
    if (!inSync)
        return QStringLiteral("正在同步 · 待应用 %1 项").arg(unapplied);
    if (!localPendings.isEmpty())
        return QStringLiteral("%1 个本地项待提交")
            .arg(localPendings.size());
    return QStringLiteral("全部已同步");
}

bool Snapshot::isBusy() const
{
    if (!dbOk)
        return false;
    const QueueStat &q = queue;
    // 待删除也算忙 —— 否则删除卡住时不会被判为卡死。
    return q.needUpCount || q.needDlCount || q.pendingFetches
        || q.dlUnstaged || q.needDelCount || !localPendings.isEmpty();
}

}  // namespace icloud
