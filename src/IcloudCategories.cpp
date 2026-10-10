#include "IcloudCategories.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "IcloudPaths.h"
#include "Log.h"

namespace icloud {

namespace {

// ── 复制 db + -wal + -shm 到进程级临时目录后只读打开 ──────────────
// 与读 iCloudDrive.db 同样的两个理由:
//   1. 直接打开会撞 iCloud 的独占锁
//   2. WAL 里未 checkpoint 的数据读不到
// 临时目录按 tag 缓存并故意不释放(随进程退出清理), 避免每次采样都新建。
struct StagedDb
{
    QSqlDatabase db;
    QString      cname;
    bool         ok = false;
};

StagedDb openStaged(const QString &src, const QString &tag, QString *err)
{
    StagedDb r;

    if (!QFile::exists(src)) {
        *err = QStringLiteral("文件不存在");
        return r;
    }

    static QHash<QString, QTemporaryDir *> dirs;
    if (!dirs.contains(tag)) {
        auto *d = new QTemporaryDir();
        if (!d->isValid()) {
            delete d;
            *err = QStringLiteral("临时目录创建失败");
            return r;
        }
        dirs.insert(tag, d);
    }
    const QString dst =
        dirs.value(tag)->filePath(QFileInfo(src).fileName());

    // 主库必须复制成功; wal/shm 可选
    QFile::remove(dst);
    if (!QFile::copy(src, dst)) {
        *err = QStringLiteral("复制主库失败");
        return r;
    }
    for (const QString &suf : { QStringLiteral("-wal"),
                                QStringLiteral("-shm") }) {
        if (!QFile::exists(src + suf))
            continue;
        QFile::remove(dst + suf);
        QFile::copy(src + suf, dst + suf);
    }

    // 连接名唯一即可; 用 tag 便于在日志里对上
    r.cname = QStringLiteral("cat_%1_%2")
                  .arg(tag)
                  .arg(QDateTime::currentMSecsSinceEpoch());
    r.db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), r.cname);
    // QSQLITE 默认不解析 file: URI, 必须开这个选项, 否则整串被当文件名
    r.db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_URI"));
    r.db.setDatabaseName(QStringLiteral("file:%1?mode=ro").arg(dst));
    if (!r.db.open()) {
        *err = QStringLiteral("打开失败: %1").arg(r.db.lastError().text());
        r.db = QSqlDatabase();
        QSqlDatabase::removeDatabase(r.cname);
        return r;
    }
    r.ok = true;
    return r;
}

void closeStaged(StagedDb &s)
{
    if (!s.ok)
        return;
    // 必须先置空句柄再 removeDatabase, 否则报 "connection is still in use"
    s.db = QSqlDatabase();
    QSqlDatabase::removeDatabase(s.cname);
    s.ok = false;
}

// 单值查询。失败返回 -1 并把原因写进 err。
int scalar(QSqlDatabase &db, const QString &sql, QString *err)
{
    // 注意: 必须显式绑定连接 —— 无参构造的 QSqlQuery 绑的是"默认连接",
    // 而这里用的是具名连接, 会报 Driver not loaded。
    QSqlQuery q(db);
    if (!q.exec(sql) || !q.next()) {
        if (err)
            *err = q.lastError().text();
        return -1;
    }
    return q.value(0).toInt();
}

// 读 UWP 后台任务的 State。exists=false 表示没有这个任务项。
int startupTaskState(const QString &task, bool *exists)
{
    QSettings reg(paths::startupTaskRoot(), QSettings::NativeFormat);
    const QString k = task + QStringLiteral("/State");
    if (!reg.contains(k)) {
        if (exists)
            *exists = false;
        return -1;
    }
    if (exists)
        *exists = true;
    return reg.value(k).toInt();
}

QString taskDesc(const QString &task)
{
    bool ex = false;
    const int st = startupTaskState(task, &ex);
    if (!ex)
        return QStringLiteral("无对应后台任务");
    return QStringLiteral("后台任务 State=%1（2=启用 0=未启用）").arg(st);
}

// 目录里的文件数(只数一层)
int countFiles(const QString &dir)
{
    QDir d(dir);
    if (!d.exists())
        return -1;
    return int(d.entryList(QDir::Files | QDir::NoDotAndDotDot).size());
}

}  // namespace

// ══════════════════════════════════════════════════════════════════
//  用户手动指定的启用状态
// ══════════════════════════════════════════════════════════════════
namespace {
QString userKey(const QString &key)
{
    return QStringLiteral("cat_%1_enabled").arg(key);
}
}  // namespace

bool IcloudCategories::hasUserValue(const QString &key)
{
    QSettings s = paths::userSettings();
    return s.contains(userKey(key));
}

bool IcloudCategories::userValue(const QString &key)
{
    QSettings s = paths::userSettings();
    return s.value(userKey(key), false).toBool();
}

void IcloudCategories::setUserValue(const QString &key, bool on)
{
    QSettings s = paths::userSettings();
    s.setValue(userKey(key), on);
    s.sync();
    log::userAction(QStringLiteral("设置 %1 启用状态=%2")
                        .arg(key, on ? QStringLiteral("启用")
                                     : QStringLiteral("未启用")));
}

void IcloudCategories::clearUserValue(const QString &key)
{
    QSettings s = paths::userSettings();
    s.remove(userKey(key));
    s.sync();
    log::userAction(QStringLiteral("清除 %1 的手动启用设置(回退自动检测)")
                        .arg(key));
}

// ══════════════════════════════════════════════════════════════════
//  四类读取
// ══════════════════════════════════════════════════════════════════
CategoryStatus IcloudCategories::readDrive(const Snapshot &snap)
{
    CategoryStatus c;
    c.key  = QStringLiteral("drive");
    c.name = QStringLiteral("云盘");

    bool ex = false;
    const int st = startupTaskState(QStringLiteral("iCloudDriveStartupTask"), &ex);
    c.autoOn  = ex && st == 2;
    c.autoWhy = taskDesc(QStringLiteral("iCloudDriveStartupTask"));

    c.hasData = snap.dbOk;
    if (!snap.dbOk) {
        c.summary = snap.stateDetail().isEmpty()
                        ? QStringLiteral("数据库不可读")
                        : snap.stateDetail();
        c.note = QStringLiteral("iCloudDrive.db 不可读, 仅能显示后台任务状态。");
        c.details.append({ QStringLiteral("后台任务"), c.autoWhy });
        return c;
    }

    const QueueStat &q = snap.queue;
    c.summary = QStringLiteral("%1 · 上传 %2 · 下载 %3 · 删除 %4")
                    .arg(snap.stateName())
                    .arg(q.needUpCount)
                    .arg(q.needDlCount)
                    .arg(q.needDelCount);

    c.details.append({ QStringLiteral("当前状态"), snap.stateName() });
    c.details.append({ QStringLiteral("同步游标"),
                       snap.hasRank
                           ? QStringLiteral("client=%1 server=%2 待应用=%3 → %4")
                                 .arg(snap.rankClient).arg(snap.rankServer)
                                 .arg(snap.unapplied)
                                 .arg(snap.inSync ? QStringLiteral("已追平")
                                                  : QStringLiteral("追赶中"))
                           : QStringLiteral("不可读") });
    c.details.append({ QStringLiteral("待上传"),
                       QStringLiteral("%1 个").arg(q.needUpCount) });
    c.details.append({ QStringLiteral("待下载"),
                       QStringLiteral("%1 个").arg(q.needDlCount) });
    c.details.append({ QStringLiteral("待删除"),
                       QStringLiteral("%1 个").arg(q.needDelCount) });
    c.details.append({ QStringLiteral("待处理项"),
                       QStringLiteral("%1 个")
                           .arg(snap.pendingItems.size()
                                + snap.pendingDirCount) });
    c.details.append({ QStringLiteral("回收站"),
                       QStringLiteral("%1 项（可恢复）").arg(snap.trashCount) });
    c.details.append({ QStringLiteral("仅云端"),
                       QStringLiteral("%1 项（按需下载）").arg(snap.cloudOnlyCount) });
    // 这一项原来叫"活跃 zone", 显示 server_zones 里 currentrequestid<>' 的
    // 数量。实测那**不是活跃信号** —— com.apple.CloudDocs 与
    // iCloud.md.obsidian 的 currentrequestid 是 iCloud 常驻注册的请求 id,
    // 常态非空、同步完成后也不清, 所以这个数字长期是 2, 毫无信息量,
    // 还会让人以为"一直有 2 个任务在跑"。
    // 换成真正反映同步进度的游标: client/server 是否追平 + 待应用条数。
    c.details.append({ QStringLiteral("同步游标"),
                       QStringLiteral("%1（待应用 %2 项）")
                           .arg(snap.inSync ? QStringLiteral("已追平")
                                            : QStringLiteral("未追平"))
                           .arg(snap.unapplied) });
    c.details.append({ QStringLiteral("后台任务"), c.autoWhy });

    c.note = QStringLiteral("云盘的全部计数都来自 iCloudDrive.db, 是真实值。");
    return c;
}

CategoryStatus IcloudCategories::readPhotos()
{
    CategoryStatus c;
    c.key  = QStringLiteral("photos");
    c.name = QStringLiteral("相册");

    bool ex = false;
    const int st = startupTaskState(QStringLiteral("iCloudPhotosStartupTask"), &ex);
    c.autoOn  = ex && st == 2;
    c.autoWhy = taskDesc(QStringLiteral("iCloudPhotosStartupTask"));

    // 媒体流库
    QString err;
    StagedDb m = openStaged(paths::mediaStreamDb(), QStringLiteral("ms"), &err);
    int nLib = -1, nAsset = -1, nDown = -1;
    if (m.ok) {
        nLib   = scalar(m.db, QStringLiteral("SELECT COUNT(*) FROM Library"), nullptr);
        nAsset = scalar(m.db, QStringLiteral("SELECT COUNT(*) FROM Asset"), nullptr);
        nDown  = scalar(m.db,
                        QStringLiteral("SELECT COUNT(*) FROM DownloadedAssets"),
                        nullptr);
        closeStaged(m);
    }

    const int thumbsHi =
        countFiles(paths::photoLibraryDir() + QStringLiteral("/ThumbsHigh"));
    const int thumbsLo =
        countFiles(paths::photoLibraryDir() + QStringLiteral("/ThumbsLow"));

    // 有数据 = 媒体流库里有图库记录, 或缩略图目录非空
    c.hasData = (nLib > 0) || (thumbsHi > 0) || (thumbsLo > 0);

    c.details.append({ QStringLiteral("图库记录"),
                       nLib < 0 ? QStringLiteral("读取失败")
                                : QStringLiteral("%1 行").arg(nLib) });
    c.details.append({ QStringLiteral("本地 Asset"),
                       nAsset < 0 ? QStringLiteral("读取失败")
                                  : QStringLiteral("%1 行").arg(nAsset) });
    c.details.append({ QStringLiteral("已下载 Asset"),
                       nDown < 0 ? QStringLiteral("读取失败")
                                 : QStringLiteral("%1 行").arg(nDown) });
    c.details.append({ QStringLiteral("本地缩略图"),
                       QStringLiteral("高 %1 / 低 %2")
                           .arg(thumbsHi < 0 ? QStringLiteral("无")
                                             : QString::number(thumbsHi))
                           .arg(thumbsLo < 0 ? QStringLiteral("无")
                                             : QString::number(thumbsLo)) });
    c.details.append({ QStringLiteral("媒体流库"), paths::mediaStreamDb() });
    c.details.append({ QStringLiteral("后台任务"), c.autoWhy });
    if (m.ok == false && !err.isEmpty())
        c.details.append({ QStringLiteral("读取媒体流库"), err });

    if (c.hasData)
        c.summary = QStringLiteral("%1 个图库 · 缩略图 %2")
                        .arg(nLib < 0 ? 0 : nLib)
                        .arg(thumbsHi < 0 ? 0 : thumbsHi);
    else
        c.summary = QStringLiteral("本地无相册数据");

    c.note = QStringLiteral(
        "iCloud 未暴露相册的同步进度, 这里只能给出本地图库与缩略图的实有数据。"
        "相册关闭时这些都会是空的。");
    return c;
}

CategoryStatus IcloudCategories::readPasswords()
{
    CategoryStatus c;
    c.key  = QStringLiteral("passwords");
    c.name = QStringLiteral("密码");

    bool ex = false;
    const int st = startupTaskState(QStringLiteral("iCloudCKKSStartupTask"), &ex);
    c.autoOn  = ex && st == 2;
    c.autoWhy = taskDesc(QStringLiteral("iCloudCKKSStartupTask"));

    QString err;
    StagedDb k = openStaged(paths::keychainDb(), QStringLiteral("kc"), &err);
    int nInet = -1, nGenp = -1, nKeys = -1;
    if (k.ok) {
        // inet = 网站/互联网密码, genp = 通用密码
        nInet = scalar(k.db, QStringLiteral("SELECT COUNT(*) FROM inet"), nullptr);
        nGenp = scalar(k.db, QStringLiteral("SELECT COUNT(*) FROM genp"), nullptr);
        nKeys = scalar(k.db, QStringLiteral("SELECT COUNT(*) FROM keys"), nullptr);
        closeStaged(k);
    }

    c.hasData = (nInet > 0) || (nGenp > 0);

    c.details.append({ QStringLiteral("网站密码"),
                       nInet < 0 ? QStringLiteral("读取失败")
                                 : QStringLiteral("%1 条").arg(nInet) });
    c.details.append({ QStringLiteral("通用密码"),
                       nGenp < 0 ? QStringLiteral("读取失败")
                                 : QStringLiteral("%1 条").arg(nGenp) });
    c.details.append({ QStringLiteral("密钥"),
                       nKeys < 0 ? QStringLiteral("读取失败")
                                 : QStringLiteral("%1 条").arg(nKeys) });
    c.details.append({ QStringLiteral("钥匙串库"), paths::keychainDb() });
    c.details.append({ QStringLiteral("后台任务"), c.autoWhy });
    if (!k.ok && !err.isEmpty() && err != QStringLiteral("文件不存在"))
        c.details.append({ QStringLiteral("读取钥匙串库"), err });

    c.summary = c.hasData
        ? QStringLiteral("本地钥匙串 %1 条")
              .arg((nInet < 0 ? 0 : nInet) + (nGenp < 0 ? 0 : nGenp))
        : QStringLiteral("本地无钥匙串数据");

    // 这一条很重要: 自动检测在这里是**不可靠的**, 明确说出来
    c.note = QStringLiteral(
        "注意: 密码的自动检测不可靠 —— 后台任务 State 实测与 iCloud 设置界面"
        "不符（State=0 但界面显示已开启）。请以手动指定为准。"
        "iCloud 也未暴露密码的同步计数, 这里只统计本地钥匙串条目数。");
    return c;
}

CategoryStatus IcloudCategories::readBookmarks()
{
    CategoryStatus c;
    c.key  = QStringLiteral("bookmarks");
    c.name = QStringLiteral("书签");

    // 书签压根没有对应的 StartupTask
    bool ex = false;
    startupTaskState(QStringLiteral("iCloudBookmarksStartupTask"), &ex);
    c.autoOn  = false;
    c.autoWhy = QStringLiteral("无对应后台任务（iCloud 没给书签建 StartupTask）");

    QString err;
    StagedDb b = openStaged(paths::bookmarksDb(), QStringLiteral("bm"), &err);
    int nCli = -1, nSrv = -1, nSync = -1, nDel = -1, nZone = -1;
    if (b.ok) {
        nCli  = scalar(b.db, QStringLiteral("SELECT COUNT(*) FROM ClientRecords"), nullptr);
        nSrv  = scalar(b.db, QStringLiteral("SELECT COUNT(*) FROM ServerRecords"), nullptr);
        nSync = scalar(b.db, QStringLiteral("SELECT COUNT(*) FROM SyncRecords"), nullptr);
        nDel  = scalar(b.db, QStringLiteral("SELECT COUNT(*) FROM DeletedRecordIDs"), nullptr);
        nZone = scalar(b.db, QStringLiteral("SELECT COUNT(*) FROM Zones"), nullptr);
        closeStaged(b);
    }

    c.hasData = (nCli > 0) || (nSrv > 0);

    c.details.append({ QStringLiteral("客户端记录"),
                       nCli < 0 ? QStringLiteral("读取失败")
                                : QStringLiteral("%1 条").arg(nCli) });
    c.details.append({ QStringLiteral("服务端记录"),
                       nSrv < 0 ? QStringLiteral("读取失败")
                                : QStringLiteral("%1 条").arg(nSrv) });
    c.details.append({ QStringLiteral("同步记录"),
                       nSync < 0 ? QStringLiteral("读取失败")
                                 : QStringLiteral("%1 条").arg(nSync) });
    c.details.append({ QStringLiteral("待删除记录"),
                       nDel < 0 ? QStringLiteral("读取失败")
                                : QStringLiteral("%1 条").arg(nDel) });
    c.details.append({ QStringLiteral("同步区"),
                       nZone < 0 ? QStringLiteral("读取失败")
                                 : QStringLiteral("%1 个").arg(nZone) });
    c.details.append({ QStringLiteral("书签库"), paths::bookmarksDb() });
    c.details.append({ QStringLiteral("后台任务"), c.autoWhy });
    if (!b.ok && !err.isEmpty() && err != QStringLiteral("文件不存在"))
        c.details.append({ QStringLiteral("读取书签库"), err });

    c.summary = c.hasData
        ? QStringLiteral("本地 %1 条 / 云端 %2 条")
              .arg(nCli < 0 ? 0 : nCli).arg(nSrv < 0 ? 0 : nSrv)
        : QStringLiteral("本地无书签数据");

    c.note = QStringLiteral(
        "注意: 书签没有对应的后台任务开关, 自动检测无法判断是否启用, "
        "请以手动指定为准。以上数字来自 ChromeBookmarks.db, 是真实记录数。");
    return c;
}

QVector<CategoryStatus> IcloudCategories::readAll(const Snapshot &snap)
{
    QVector<CategoryStatus> v;
    v.append(readDrive(snap));
    v.append(readPhotos());
    v.append(readPasswords());
    v.append(readBookmarks());

    // 生效值: 用户手动指定优先
    for (CategoryStatus &c : v) {
        c.userSet = hasUserValue(c.key);
        c.enabled = c.userSet ? userValue(c.key) : c.autoOn;
    }
    return v;
}

}  // namespace icloud
