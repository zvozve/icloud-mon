// ======================================================================
//  IcloudPaths.h  -  集中管理本机 iCloud 相关路径与常量
//
//  设计要点:
//    所有路径集中在此, 不散落在各 .cpp。改机器只需改这一个文件。
//    路径支持环境变量覆盖, 便于在非本机环境调试。
// ======================================================================
#ifndef ICLOUDPATHS_H
#define ICLOUDPATHS_H

#include <QString>
#include <QStringList>

namespace icloud {

// ── 默认路径 (可被环境变量覆盖) ────────────────────────────────────
namespace paths {

// iCloud Drive 本地同步根目录
inline QString driveRoot()
{
    const QByteArray e = qgetenv("ICLOUD_DRIVE_ROOT");
    return e.isEmpty() ? QStringLiteral("C:/Container/Cloud/iCloudDrive")
                       : QString::fromLocal8Bit(e);
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
