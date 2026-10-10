// ======================================================================
//  IcloudPaths.h  -  集中管理本机 iCloud 相关路径与常量
//
//  设计要点:
//    所有路径集中在此, 不散落在各 .cpp。改机器只需改这一个文件。
//    路径支持环境变量覆盖, 便于在非本机环境调试。
// ======================================================================
#ifndef ICLOUDPATHS_H
#define ICLOUDPATHS_H

#include <QDir>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QStandardPaths>

namespace icloud {

// ── 默认路径 (可被环境变量覆盖) ────────────────────────────────────
namespace paths {

// ── 用户可配置的路径 (持久化) ──────────────────────────────────────
//
// 为什么单独存QSettings 而不是继续用环境变量:
// 环境变量是"临时调试手段", 每次开新终端都要重设, 不适合当用户配置。
//优先级: 环境变量 > 注册表保存值 > 内置默认值。
// 环境变量留在最外层, 保证调试时仍能用它临时改指向而不动注册表。

inline QSettings userSettings()
{
    // 组织名与应用名在 main.cpp 里已设成 zvozve / iCloudMon,
    // 这里显式再写一遍, 避免以后调用顺序变了导致读到别处的配置。
    return QSettings(QSettings::NativeFormat, QSettings::UserScope,
                     QStringLiteral("zvozve"), QStringLiteral("iCloudMon"));
}

// iCloud Drive 本地同步根目录
inline QString driveRoot()
{
    const QByteArray e = qgetenv("ICLOUD_DRIVE_ROOT");
    if (!e.isEmpty())
        return QString::fromLocal8Bit(e);

    QSettings s = userSettings();
    const QString saved = s.value(QStringLiteral("driveRoot")).toString();
    if (!saved.isEmpty())
        return saved;

    return QStringLiteral("C:/Container/Cloud/iCloudDrive");
}

// 保存用户选择的同步根目录。
// 返回 false 并填 error 表示没写成功(通常是注册表权限问题)。
inline bool setDriveRoot(const QString &path, QString *error)
{
    QSettings s = userSettings();
    s.setValue(QStringLiteral("driveRoot"), path);
    s.sync();
    if (s.status() != QSettings::NoError) {
        if (error)
            *error = QStringLiteral("写注册表失败: %1").arg(int(s.status()));
        return false;
    }
    return true;
}

// 清掉保存值, 回退到内置默认路径
inline void clearDriveRoot()
{
    QSettings s = userSettings();
    s.remove(QStringLiteral("driveRoot"));
    s.sync();
}

// 当前用户的 Local AppData 根目录 (C:/Users/<当前用户>/AppData/Local)。
// ⚠ 之前硬编码成 C:/Users/USIDE/... —— 换用户名/换电脑即失效。
// 这里改走 LOCALAPPDATA 环境变量 (Windows 必有), 缺失时再用
// QStandardPaths 兜底, 彻底消除"写死用户名"问题。
inline QString localAppData()
{
    QString p = QString::fromLocal8Bit(qgetenv("LOCALAPPDATA"));
    if (!p.isEmpty())
        return QDir::fromNativeSeparators(p);
    QString alt = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation);
    if (!alt.isEmpty()) {
        QDir d(alt);
        d.cdUp(); d.cdUp();   // 去掉 /<org>/<app>, 回到 Local 根
        return QDir::fromNativeSeparators(d.absolutePath());
    }
    return QDir::homePath() + QStringLiteral("/AppData/Local");
}

// Store 版 iCloud 的包目录
//
// 包族名 AppleInc.iCloud_nzyj5cx40ttqa 的发布者 ID 后缀全球一致
// (由 Apple 证书决定, 与机器/用户无关), 默认直接用即可。
// 仅当默认名在 Packages 下不存在时, 才退化为枚举 AppleInc.iCloud* 前缀,
// 兼容极少数发布者 ID 不同的极端情况。
inline QString packageDir()
{
    const QByteArray e = qgetenv("ICLOUD_PKG_DIR");
    if (!e.isEmpty())
        return QString::fromLocal8Bit(e);

    const QString base = localAppData()
        + QStringLiteral("/Packages");
    const QString fixed = base
        + QStringLiteral("/AppleInc.iCloud_nzyj5cx40ttqa");
    if (QDir(fixed).exists())
        return fixed;

    const QStringList hits = QDir(base).entryList(
        QStringList() << QStringLiteral("AppleInc.iCloud*"),
        QDir::Dirs | QDir::NoDotAndDotDot);
    if (!hits.isEmpty())
        return base + QStringLiteral("/") + hits.first();

    return fixed;   // 都没找到也返回默认, 让上层报"数据库不存在"而非崩溃
}

// iCloudDrive.db 所在目录 (LocalCache/Local/Apple Inc/iCloudDrive)
inline QString dbDir()
{
    return packageDir()
        + QStringLiteral("/LocalCache/Local/Apple Inc/iCloudDrive");
}

// 主数据库
inline QString dbFile()
{
    return dbDir() + QStringLiteral("/iCloudDrive.db");
}

// CloudFiles 驱动事件日志
inline QString eventLog()
{
    return QStringLiteral(
        "Microsoft-Windows-CloudFiles-Filter/Operational");
}

// ── 其余三类同步的数据源 ──────────────────────────────────────────
// 云盘在 iCloudDrive.db(见上面 dbFile()), 相册/密码/书签各有各的库。

// 书签: Chrome 书签同步库
inline QString bookmarksDb()
{
    return packageDir()
        + QStringLiteral("/LocalCache/Local/Apple Inc/ChromeBookmarks/ChromeBookmarks.db");
}

// 密码: 钥匙串库 (inet=网站密码 genp=通用密码 crec=证书)
inline QString keychainDb()
{
    return packageDir()
        + QStringLiteral("/LocalCache/Roaming/Apple Computer/Keychains/keychain-2.db");
}

// 相册: 媒体流库
inline QString mediaStreamDb()
{
    return packageDir()
        + QStringLiteral("/LocalCache/Roaming/Apple Computer/MediaStream/local.db");
}

// 相册: 本地缩略图目录
inline QString photoLibraryDir()
{
    return packageDir()
        + QStringLiteral("/LocalCache/Local/Apple Inc/iCloudPhotoLibrary");
}

// iCloud 存储配额/用量: iCloud 把"设置→iCloud 存储空间"那张卡片的全部
// 数据缓存在这个二进制 plist 里(StorageData 节点), 我们只读它。
// 见 IcloudQuota.h 的详细说明。
inline QString quotaPrefFile()
{
    return packageDir()
        + QStringLiteral("/LocalCache/Roaming/Apple Computer/Preferences")
        + QStringLiteral("/iCloudWinPref.plist");
}

// UWP 后台任务的注册表根键。各任务的 State: 2=启用 0=未启用。
// ⚠ 实测只有云盘和相册与 iCloud 设置界面吻合; 密码/书签对不上, 详见
// IcloudCategories.h 的说明 —— 只能当参考值。
inline QString startupTaskRoot()
{
    return QStringLiteral(
        "HKEY_CURRENT_USER\\Software\\Classes\\Local Settings\\Software\\"
        "Microsoft\\Windows\\CurrentVersion\\AppModel\\SystemAppData\\"
        "AppleInc.iCloud_nzyj5cx40ttqa");
}

// 状态历史 (卡死检测基线)
inline QString stateFile()
{
    return QStringLiteral("icloud_state.jsonl");
}

// ── 需要管理的 iCloud 进程 ─────────────────────────────────────────
// 重启时全部结束, 再重新拉起 Store 应用。
inline QStringList icloudProcessNames()
{
    return {
        QStringLiteral("iCloudDrive.exe"),
        QStringLiteral("iCloudHome.exe"),
        QStringLiteral("iCloudCKKS.exe"),
        QStringLiteral("ApplePhotoStreams.exe"),
    };
}

// Store 应用的 AUMID, 用于 explorer.exe 重新拉起。
//
// 格式: <PackageFamilyName>!<Application Id>
//   PackageFamilyName = AppleInc.iCloud_nzyj5cx40ttqa
//                       (Name + "_" + PublisherId, 见包安装目录名)
//   Application Id    = iCloud
//                       (取自 AppxManifest.xml 的 <Application Id="iCloud">)
//
// ★ 这里以前写的是 "Microsoft.Store_8wekyb3d8bbwe!App" —— 那是**微软商店**
// 的 AUMID, 不是 iCloud 的。后果是"重启 iCloud"只能杀掉、拉不起来:
// explorer 去开商店, iCloud 一直不启动。
// 核实方式(别猜): 看包安装目录 AppxManifest.xml 里的 Application Id
//   ls "/c/Program Files/WindowsApps/AppleInc.iCloud"*/
//   grep -oE '<Application Id="[^"]+"' <包目录>/AppxManifest.xml
inline QString icloudAumid()
{
    return QStringLiteral(
        "AppleInc.iCloud_nzyj5cx40ttqa!iCloud");
}

// 自启动注册表键 (用户级, 免管理员)
inline QString autoStartRegistryKey()
{
    return QStringLiteral(
        "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
}

}  // namespace paths

}  // namespace icloud

#endif  // ICLOUDPATHS_H
