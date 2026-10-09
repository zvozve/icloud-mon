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

// Store 应用的 AUMID, 用于 explorer.exe 重新拉起
inline QString icloudAumid()
{
    return QStringLiteral(
        "Microsoft.Store_8wekyb3d8bbwe!App");
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
