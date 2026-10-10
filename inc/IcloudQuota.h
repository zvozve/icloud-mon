// ======================================================================
//  IcloudQuota.h  -  iCloud 存储配额/用量
//
//  数据源: <包目录>/LocalCache/Roaming/Apple Computer/Preferences/
//          iCloudWinPref.plist 里的 StorageData 节点
//
//  ★ 为什么能读到: Windows 版 iCloud(S store 版)把"设置 → iCloud 存储空间"
//    那个卡片的全部数据缓存在这个偏好文件里, 实测字段:
//      quota_info_in_bytes: total_quota / total_used / total_available
//      storage_usage_by_media: [{media_key, display_label,
//                                 usage_in_bytes, display_color}, ...]
//      quota_status: over_quota / paid_quota / have_max_quota_tier
//      entry_points: quota.manage_storage_url (Manage 按钮的跳转地址)
//
//    它是二进制 plist(bplist00), 用 QPlist 解析 —— Qt 原生支持, 无需第三方。
//    这个文件由 iCloud 自己维护更新, 我们只读。
// ======================================================================
#pragma once

#include <QString>
#include <QVector>

namespace icloud {

// 一个分类的用量(照片/备份/文档/信息/邮件…)
struct QuotaBucket
{
    QString key;        // photos / backup / docs / messages / mail
    QString label;      // "Photos and Videos"(界面直接用这个, 与 Apple 一致)
    qint64  bytes  = 0; // 该分类已用字节
    QString colorHex;   // Apple 自带的显示色(ffcc00 / 5856d6 / ...), 无则空
};

// 配额总览。ok=false 时 error 说明原因, 其余字段无意义。
struct QuotaInfo
{
    bool    ok         = false;
    QString error;

    qint64  totalQuota = 0;      // 套餐总量
    qint64  totalUsed  = 0;      // 已用
    qint64  totalFree  = 0;      // 剩余 = totalQuota - totalUsed

    bool    paidQuota  = false;  // 是否付费套餐
    bool    overQuota  = false;  // 是否已超额

    QVector<QuotaBucket> buckets;

    // Manage 按钮的跳转地址(Apple 给的, 形如
    // https://p228-quota.icloud.com.cn:443/quotaservice/external/.../getManageCloudStorage)
    QString manageUrl;
};

// 读取一次配额。**每次调用都重新读文件** ——
// iCloud 会随时更新这个文件(配额变化/升降级), 缓存反而会显示过期数据。
// 文件很小(约 1KB)且本地读, 开销可忽略。
QuotaInfo readQuota();

} // namespace icloud