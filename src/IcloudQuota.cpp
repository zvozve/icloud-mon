// ======================================================================
//  IcloudQuota.cpp  -  iCloud 存储配额/用量读取
//  数据源说明见 IcloudQuota.h
// ======================================================================
#include "IcloudQuota.h"
#include "IcloudPaths.h"
#include "BPlist.h"
#include "Log.h"

#include <QFile>

namespace icloud {

QuotaInfo readQuota()
{
    QuotaInfo q;

    const QString path = paths::quotaPrefFile();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        q.error = QStringLiteral("读不到 iCloud 偏好文件(%1)")
                      .arg(f.errorString());
        log::warn(QStringLiteral("[QUOTA] %1: %2")
                      .arg(q.error).arg(path));
        return q;
    }

    // 二进制 plist(bplist00)。Qt5 没有 plist 解析器, 用自带的最小实现
    // (见 BPlist.h)。解析失败时整个丢弃 —— 不做"部分可用",
    // 免得界面显示一个缺字段的半成品配额卡片。
    const QVariant root = parseBplist(f.readAll());
    if (!root.isValid() || !root.canConvert<QVariantMap>()) {
        q.error = QStringLiteral("iCloud 偏好文件解析失败"
                                 "(格式可能是 Apple 改过的)");
        log::warn(QStringLiteral("[QUOTA] %1: %2")
                  .arg(q.error).arg(path));
        return q;
    }

    const QVariantMap rootMap = root.toMap();
    const QVariantMap sd = rootMap.value(QStringLiteral("StorageData")).toMap();
    if (sd.isEmpty()) {
        q.error = QStringLiteral("偏好文件里没有 StorageData 节点"
                                 "(iCloud 版本不同或尚未刷新过配额)");
        log::warn(QStringLiteral("[QUOTA] %1: %2").arg(q.error).arg(path));
        return q;
    }

    // ── 总量 / 已用 / 剩余 ──
    // 取值优先级: quota_info_in_bytes → storage_info_in_bytes。
    // 后者是更早的结构名, 保留兜底以防 Apple 改动。
    QVariantMap qi = sd.value(QStringLiteral("quota_info_in_bytes")).toMap();
    if (qi.isEmpty())
        qi = sd.value(QStringLiteral("storage_info_in_bytes")).toMap();

    q.totalQuota = qi.value(QStringLiteral("total_quota")).toLongLong();
    q.totalUsed  = qi.value(QStringLiteral("total_used")).toLongLong();
    if (q.totalFree <= 0 && q.totalQuota > 0)
        q.totalFree = q.totalQuota - q.totalUsed;

    // ── 套餐状态 ──
    const QVariantMap qs = sd.value(QStringLiteral("quota_status")).toMap();
    q.paidQuota = qs.value(QStringLiteral("paid_quota")).toBool();
    q.overQuota = qs.value(QStringLiteral("over_quota")).toBool();

    // ── 各分类用量 ──
    const QVariantList bl =
        sd.value(QStringLiteral("storage_usage_by_media")).toList();
    for (const QVariant &v : bl) {
        const QVariantMap m = v.toMap();
        QuotaBucket b;
        b.key      = m.value(QStringLiteral("media_key")).toString();
        b.label    = m.value(QStringLiteral("display_label")).toString();
        b.bytes    = m.value(QStringLiteral("usage_in_bytes")).toLongLong();
        b.colorHex = m.value(QStringLiteral("display_color")).toString();
        // Apple 没给颜色时按 media_key 兜一个(与 Apple 界面同色系),
        // 免得条形图少一段就没颜色。
        if (b.colorHex.isEmpty()) {
            if (b.key == QLatin1String("photos"))         b.colorHex = QStringLiteral("ffcc00");
            else if (b.key == QLatin1String("backup"))     b.colorHex = QStringLiteral("5856d6");
            else if (b.key == QLatin1String("docs"))       b.colorHex = QStringLiteral("ff9500");
            else if (b.key == QLatin1String("messages"))   b.colorHex = QStringLiteral("34c759");
            else if (b.key == QLatin1String("mail"))       b.colorHex = QStringLiteral("007aff");
        }
        if (!b.label.isEmpty())
            q.buckets.append(b);
    }

    // ── Manage 跳转地址 ──
    q.manageUrl = sd.value(QStringLiteral("entry_points")).toMap()
                      .value(QStringLiteral("quota.manage_storage_url"))
                      .toString();

    q.ok = true;

    // 只在数值变化时记日志, 免得每 3 秒刷一条一样的。
    static qint64 lastQuota = -1, lastUsed = -1;
    if (q.totalQuota != lastQuota || q.totalUsed != lastUsed) {
        log::info(QStringLiteral("[QUOTA] 总量 %1 已用 %2 分类 %3 项")
                      .arg(q.totalQuota).arg(q.totalUsed)
                      .arg(q.buckets.size()));
        lastQuota = q.totalQuota;
        lastUsed  = q.totalUsed;
    }

    return q;
}

} // namespace icloud