// ======================================================================
//  IcloudCategories.h  -  iCloud 四类同步 (云盘/相册/密码/书签) 的状态读取
//
//  为什么要单独一个模块:
//    云盘的数据在 iCloudDrive.db, 其余三类各有各的数据源 ——
//      书签  : LocalCache/Local/Apple Inc/ChromeBookmarks/ChromeBookmarks.db
//      密码  : LocalCache/Roaming/Apple Computer/Keychains/keychain-2.db
//      相册  : LocalCache/Roaming/Apple Computer/MediaStream/local.db
//    以及各类的 UWP 后台任务开关(注册表 StartupTask\State)
//
//  ⚠ 关于"是否启用"的重要说明:
//    注册表的 StartupTask\State(2=启用 0=未启用) 只对**云盘和相册**与
//    iCloud 设置界面吻合(实测核对过)。密码和书签对不上:
//      密码: State=0 但设置界面显示 On
//      书签: 根本没有对应的 StartupTask
//    真正的开关存在 Settings/settings.dat 里, 而那个文件被 iCloudHome
//    独占锁定(复制和读取都失败, Device or resource busy), 拿不到权威值。
//    所以本模块只把自动检测结果当作**参考值**输出(autoOn/autoWhy),
//    最终生效值由用户在设置页手动指定(enabled), 绝不假装检测准确。
// ======================================================================
#ifndef ICLOUDCATEGORIES_H
#define ICLOUDCATEGORIES_H

#include <QString>
#include <QVector>

#include "IcloudSnapshot.h"

namespace icloud {

// 一条明细(标签 + 值)。页面上按行铺开显示。
struct CatDetail
{
    QString label;
    QString value;
};

struct CategoryStatus
{
    QString key;          // "drive" | "photos" | "passwords" | "bookmarks"
    QString name;         // 云盘 / 相册 / 密码 / 书签

    // 自动检测(仅作参考值显示)
    bool    autoOn = false;
    QString autoWhy;      // 检测依据, 显示给用户便于纠错

    // 生效值 = 用户手动指定优先, 否则用自动检测
    bool    enabled = false;
    bool    userSet = false;   // 用户是否手动设过

    bool    hasData = false;   // 是否读到了真实数据
    QString summary;           // 首页卡片上的一句话
    QString note;              // 该类别页的补充说明(例如"iCloud 未暴露同步计数")
    QVector<CatDetail> details;
};

// 四类的固定顺序(与 iCloud 设置界面一致)
inline QStringList categoryKeys()
{
    return { QStringLiteral("drive"), QStringLiteral("photos"),
             QStringLiteral("passwords"), QStringLiteral("bookmarks") };
}

class IcloudCategories
{
public:
    // 读四类的当前状态。云盘复用已采样的 Snapshot, 其余三类读各自的库。
    static QVector<CategoryStatus> readAll(const Snapshot &snap);

    // 用户手动指定的启用状态(存在 HKCU\Software\zvozve\iCloudMon)
    static bool hasUserValue(const QString &key);
    static bool userValue(const QString &key);        // 无值时返回 false
    static void setUserValue(const QString &key, bool on);
    static void clearUserValue(const QString &key);

private:
    static CategoryStatus readDrive(const Snapshot &snap);
    static CategoryStatus readPhotos();
    static CategoryStatus readPasswords();
    static CategoryStatus readBookmarks();
};

}  // namespace icloud

#endif  // ICLOUDCATEGORIES_H
