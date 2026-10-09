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

// Store 版 iCloud 的包目录
inline QString packageDir()
{
    const QByteArray e = qgetenv("ICLOUD_PKG_DIR");
    return e.isEmpty()
        ? QStringLiteral("C:/Users/USIDE/AppData/Local/Packages/"
                         "AppleInc.iCloud_nzyj5cx40ttqa")
        : QString::fromLocal8Bit(e);
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
