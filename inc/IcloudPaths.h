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
#include <QFileInfo>
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
//优先级: 环境变量 > 注册表保存值 > 数据库推导(仅 Python 侧) > %USERPROFILE%\iCloudDrive。
// 环境变量留在最外层, 保证调试时仍能用它临时改指向而不动注册表。

inline QSettings userSettings()
{
    // 组织名与应用名在 main.cpp 里已设成 iCloudMon / iCloudMon,
    // 这里显式再写一遍, 避免以后调用顺序变了导致读到别处的配置。
    // ★ 组织名曾经是 zvozve(个人账号名), 对通用软件是错的 —— 见
    //   main.cpp 的 migrateLegacyUserData(): 老用户升级时会把
    //   HKCU\Software\zvozve\iCloudMon 一次性搬到 HKCU\Software\iCloudMon\iCloudMon。
    return QSettings(QSettings::NativeFormat, QSettings::UserScope,
                     QStringLiteral("iCloudMon"), QStringLiteral("iCloudMon"));
}

// iCloud Drive 本地同步根目录。
//
// ★ 这里曾经硬编码成 "C:/Container/Cloud/iCloudDrive" —— 那是**作者本机
//   自己改过的位置**, 不是通用默认值。标准安装下同步根是
//   %USERPROFILE%\iCloudDrive, 硬编码会让换台机器就"打开目录"点不动、
//   .Trash 找不到、待处理项路径全错。
//
//   现在的取法(与 Python 侧 paths.py 保持同一优先级):
//     ① 环境变量(调试)  ICLOUD_DRIVE_ROOT
//     ② 注册表         用户在设置页显式选过的路径(= 用户覆盖)
//     ③ 数据库自动推导  ← **真正的权威源**, 见下
//     ④ %USERPROFILE%\iCloudDrive  ← 仅本级兜底(标准安装位置, 机器无关)
//
//   ③ 只在 Python 侧实现: iCloudDrive.db 的 local_items.item_absolutepath
//   存着 iCloud 自己写的完整本地绝对路径(机器无关), 能反推出真正的根目录;
//   推导结果随 payload 下发, Bridge::driveRoot() 优先用它。C++ 这层不做
//   推导(避免同一件事两处实现), 只保留 ①②④ 作为旁路离线时的兜底。
inline QString driveRoot()
{
    const QByteArray e = qgetenv("ICLOUD_DRIVE_ROOT");
    if (!e.isEmpty())
        return QString::fromLocal8Bit(e);

    QSettings s = userSettings();
    const QString saved = s.value(QStringLiteral("driveRoot")).toString();
    if (!saved.isEmpty())
        return saved;

    return QDir::homePath() + QStringLiteral("/iCloudDrive");
}

// 用户是否显式覆盖过同步根目录(注册表里有 driveRoot 值)。
// 设置页的「默认」按钮据此决定显不显示 —— 以前是拿根目录去和那串
// 硬编码字符串做比较, 硬编码一删它就没意义了。
inline bool driveRootIsCustom()
{
    QSettings s = userSettings();
    return s.contains(QStringLiteral("driveRoot"));
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
//
// ★ 包族名不再写死: 直接取 packageDir() 的末级目录名(即 PackageFamilyName),
//   与 packageDir() 的枚举逻辑保持一致 —— 万一 Apple 改了发布者 Id 后缀,
//   枚举到的新名字会自动生效, 不会因为写死而启动失败。
//   packageDir() 在找不到时也会回退到原写死值, 所以本函数行为不会比之前更差。
//   Application Id 默认 "iCloud", 可用环境变量 ICLOUD_APP_ID 覆盖便于调试。
inline QString icloudAumid()
{
    const QByteArray envApp = qgetenv("ICLOUD_APP_ID");
    const QString appId = envApp.isEmpty()
        ? QStringLiteral("iCloud")
        : QString::fromLocal8Bit(envApp);

    const QString family = QFileInfo(packageDir()).fileName();
    return family + QStringLiteral("!") + appId;
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
