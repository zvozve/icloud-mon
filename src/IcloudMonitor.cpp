#include "IcloudMonitor.h"

#include <algorithm>   // std::sort (Qt5 无 <QAlgorithm>, 那是 Qt6 的名字)

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QSqlDatabase>
#include <QSqlDriver>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

#include "IcloudPaths.h"
#include "Log.h"

namespace icloud {

// ======================================================================
//  Snapshot 派生方法
// ======================================================================
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
    if (!localPendings.isEmpty())
        return QStringLiteral("上传处理中");
    if (!activeZones.isEmpty())
        return QStringLiteral("同步中");
    return QStringLiteral("空闲");
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
    if (!localPendings.isEmpty())
        return QStringLiteral("%1 个本地项有未提交变更").arg(localPendings.size());
    if (!activeZones.isEmpty())
        return QStringLiteral("%1 个 zone 有活跃请求").arg(activeZones.size());
    return QStringLiteral("无待处理任务");
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

// ======================================================================
//  IcloudMonitor
// ======================================================================
IcloudMonitor::IcloudMonitor(QObject *parent)
    : QObject(parent)
{
    // 历史基线放 AppData 而不是 exe 同级:
    // exe 可能在 Program Files 下只读, 写不进去卡死检测就彻底失效了。
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    m_stateFile = dir + QDir::separator() + paths::stateFile();

    log::info(QStringLiteral("[RUN] IcloudMonitor 启动 | 历史基线: %1")
                  .arg(m_stateFile));
}

IcloudMonitor::~IcloudMonitor()
{
    stop();
    log::info(QStringLiteral("[RUN] IcloudMonitor 退出"));
}

QString IcloudMonitor::stateFilePath() const
{
    return m_stateFile;
}

void IcloudMonitor::resetHistory()
{
    QFile::remove(m_stateFile);
    m_lastFp = QJsonObject();
    m_lastAnomalous = false;
}

void IcloudMonitor::start(int intervalMs)
{
    if (!m_timer) {
        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, &IcloudMonitor::onTimeout);
    }
    m_timer->start(intervalMs);
    refresh();
}

void IcloudMonitor::stop()
{
    if (m_timer)
        m_timer->stop();
}

void IcloudMonitor::refresh()
{
    // 全程主线程: 见头文件 doSample 上方的线程约束说明。
    // 实测16MB 文件复制 + SQLite 查询在 SSD 上是毫秒级, 不卡 UI。
    doSample();
}

void IcloudMonitor::onTimeout()
{
    refresh();
}

// ── 复制 db + wal + shm 到临时目录 ────────────────────────────────
bool IcloudMonitor::stageDatabase(QString &dbPath, QString &err)
{
    const QString src = paths::dbFile();
    if (!QFile::exists(src)) {
        err = QStringLiteral("数据库不存在: %1").arg(src);
        return false;
    }

    static QTemporaryDir *s_tmp = nullptr;
    if (!s_tmp)
        s_tmp = new QTemporaryDir();   // 进程级, 随进程退出清理
    if (!s_tmp->isValid()) {
        err = QStringLiteral("无法创建临时目录");
        return false;
    }

    const QString dst = s_tmp->filePath(QStringLiteral("iCloudDrive.db"));
    auto copyOne = [&](const QString &name) -> bool {
        const QString s = src + name;
        if (!QFile::exists(s))
            return true;                       // wal/shm 可能不存在, 可选
        QFile::remove(dst + name);
        return QFile::copy(s, dst + name);
    };

    // 三个都必须复制, 少一个都会导致队列读不到
    if (!QFile::remove(dst) && QFile::exists(dst)) {
        err = QStringLiteral("临时库文件被占用");
        return false;
    }
    if (!copyOne(QString()) || !copyOne(QStringLiteral("-wal"))
        || !copyOne(QStringLiteral("-shm"))) {
        err = QStringLiteral("复制数据库文件失败");
        return false;
    }

    dbPath = dst;
    return true;
}

// ── 建立只读连接 (必须在主线程调用) ──────────────────────────────
//
// ★ 关键坑 (别再改回去): 每个 QSqlQuery 都必须显式绑定连接 ────────
// 本函数用的是**具名连接** "icm_<时间戳>", 而不是默认连接。
// 而 `QSqlQuery q;` 这种无参构造绑定的是**默认连接** (QSqlDatabase::database()
// 返回的那个)。这里从没建过默认连接, 所以:
//     db.open() 返回 true   ← 数据库确实打开了
//     但 q.exec() 报 "Driver not loaded" + "database not open"
// 表现极具迷惑性: 看起来是驱动加载失败, 实际是查询压根没问到我们这条连接。
// 这个症状排查了很久, 期间误以为是插件路径 / DLL 位数 / 线程 / 连接生命周期
// 问题, 全部排除后才定位到这里。
// 正确写法: QSqlQuery q(db);   ← 必须把连接传进去
//
// 另一个坑: Qt 的 QSQLITE 默认不解析 file: URI, 会把
// "file:C:/xxx.db?mode=ro" 整串当文件名, 必须开 QSQLITE_OPEN_URI。
bool IcloudMonitor::openConnection(const QString &dbPath, QString &err,
                                   QSqlDatabase *out)
{
    if (dbPath.isEmpty()) {
        err = QStringLiteral("数据库快照路径为空");
        return false;
    }

    // 唯一连接名(带时间戳), 便于排查时在日志里对上
    const QString cname = QStringLiteral("icm_%1")
                              .arg(QDateTime::currentMSecsSinceEpoch());

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                cname);
    db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_URI"));
    db.setDatabaseName(QStringLiteral("file:%1?mode=ro").arg(dbPath));
    if (!db.open()) {
        err = QStringLiteral("打开数据库失败: %1").arg(db.lastError().text());
        db = QSqlDatabase();          // 先松手, 否则 removeDatabase 拒绝执行
        QSqlDatabase::removeDatabase(cname);
        return false;
    }

    *out = db;
    return true;
}

void IcloudMonitor::closeConnection(QSqlDatabase *db)
{
    if (!db || !db->isValid())
        return;
    const QString name = db->connectionName();
    db->close();
    // 关键: 必须先置空再removeDatabase, 否则 Qt 报
    // "connection is still in use" 并拒绝注销(连接泄漏累积)。
    *db = QSqlDatabase();
    QSqlDatabase::removeDatabase(name);
}

// ── 读取全部字段 (必须在主线程调用) ───────────────────────────────
bool IcloudMonitor::readSnapshot(Snapshot &out, const QString &dbPath,
                                 QString &err)
{
    // 连接严格局部化: 建 -> 用 -> 一次性销, 生命周期不跨函数
    QSqlDatabase db;
    if (!openConnection(dbPath, err, &db))
        return false;

    // 诊断只打一次: 14 条查询会全部失败, 逐条打诊断会淹掉日志
    bool diagDumped = false;
    auto exec = [&](const QString &sql, QSqlQuery &q) -> bool {
        if (q.exec(sql))
            return true;
        // 逐条记下失败的 SQL: 否则上层只看到"数据库不可读"这种结论,
        // 无从知道是哪条查询挂了。
        log::error(QStringLiteral("[SQL] %1 -> %2")
                       .arg(sql.left(90), q.lastError().text()));

        // 一次性把连接的真实状态打出来。
        // "Driver not loaded" 可能来自三个完全不同的原因, 光看错误文本
        // 分不出来: 驱动没装 / 连接没注册 / 连接注册了但没打开。
        if (!diagDumped) {
            diagDumped = true;
            QSqlDriver *drv = db.driver();
            log::error(QStringLiteral(
                           "[SQL-DIAG] isValid=%1 isOpen=%2 drv=%3 drvOpen=%4")
                           .arg(int(db.isValid()))
                           .arg(int(db.isOpen()))
                           .arg(int(drv != nullptr))
                           .arg(int(drv && drv->isOpen())));
            log::error(QStringLiteral("[SQL-DIAG] connName=%1 dbName=%2")
                           .arg(db.connectionName(), db.databaseName()));
            log::error(QStringLiteral("[SQL-DIAG] 已注册连接=[%1]")
                           .arg(QSqlDatabase::connectionNames().join(
                               QLatin1Char(','))));
            log::error(QStringLiteral("[SQL-DIAG] 可用驱动=[%1] 库路径=[%2]")
                           .arg(QSqlDatabase::drivers().join(QLatin1Char(',')),
                                QCoreApplication::libraryPaths().join(
                                    QLatin1Char(';'))));
        }
        return false;
    };

    // 1) 客户端版本
    {
        QSqlQuery q(db);
        if (exec(QStringLiteral("SELECT version FROM app_info LIMIT 1"), q)
            && q.next())
            out.clientVersion = q.value(0).toString();
    }

    // 2) 同步游标
    {
        QSqlQuery qc(db), qs(db), qu(db);
        bool cok = exec(QStringLiteral(
            "SELECT next_syncdown_rank FROM client_state LIMIT 1"), qc)
            && qc.next();
        bool sok = exec(QStringLiteral(
            "SELECT next_syncdown_rank FROM server_state LIMIT 1"), qs)
            && qs.next();
        exec(QStringLiteral("SELECT COUNT(*) FROM unapplied_ranks"), qu);
        if (qu.next())
            out.unapplied = qu.value(0).toInt();
        if (cok && sok) {
            out.hasRank= true;
            out.rankClient = qc.value(0).toLongLong();
            out.rankServer = qs.value(0).toLongLong();
            out.inSync = (out.rankClient == out.rankServer) && (out.unapplied == 0);
        } else {
            log::warn(QStringLiteral("[SQL] 同步游标不可读 cok=%1 sok=%2")
                          .arg(int(cok)).arg(int(sok)));
        }
    }

    // 3) 队列计数 (item_recursive_properties)
    {
        QSqlQuery q(db);
        if (exec(QStringLiteral(
                "SELECT COALESCE(SUM(needs_upload_count),0),"
                "       COALESCE(SUM(needs_upload_size),0),"
                "       COALESCE(SUM(needs_download_count),0),"
                "       COALESCE(SUM(needs_download_size),0),"
                "       COALESCE(SUM(synced_up_count),0),"
                "       COALESCE(SUM(uploaded_count),0),"
                "       COALESCE(SUM(uploaded_size),0),"
                "       COALESCE(SUM(downloaded_count),0),"
                "       COALESCE(SUM(downloaded_size),0),"
                "       COALESCE(SUM(dl_unstaged_count),0),"
                "       COALESCE(SUM(needs_dl_unstage_count),0),"
                "       COALESCE(SUM(faults_count),0),"
                "       COALESCE(SUM(over_quota_count),0),"
                "       COALESCE(SUM(needs_sync_up_count),0),"
                "       COALESCE(SUM(needs_delete_doc_count),0) "
                "FROM item_recursive_properties"), q)) {
            if (q.next()) {
                QueueStat &s = out.queue;
                s.needUpCount   = q.value(0).toInt();
                s.needUpBytes   = q.value(1).toLongLong();
                s.needDlCount   = q.value(2).toInt();
                s.needDlBytes   = q.value(3).toLongLong();
                s.syncedUpCount = q.value(4).toInt();
                s.uploadedCount = q.value(5).toInt();
                s.uploadedBytes = q.value(6).toLongLong();
                s.downloadedCount   = q.value(7).toInt();
                s.downloadedBytes   = q.value(8).toLongLong();
                s.dlUnstaged    = q.value(9).toInt();
                s.needDlUnstage = q.value(10).toInt();
                out.faults      = q.value(11).toInt();
                out.overQuota   = q.value(12).toInt();
                s.needSyncUp    = q.value(13).toInt();
                s.needDelCount  = q.value(14).toInt();
            }
        } else {
            err = QStringLiteral("读 item_recursive_properties 失败: %1")
                .arg(q.lastError().text());
        }
    }

    // 4) 待取回队列
    {
        QSqlQuery q(db);
        if (exec(QStringLiteral(
                "SELECT COUNT(*) FROM server_items_pending_fetches"), q)
            && q.next())
            out.queue.pendingFetches = q.value(0).toInt();
    }

    // 5) 云端状态分布 (pcs_state)
    {
        QSqlQuery q(db);
        if (exec(QStringLiteral(
                "SELECT pcs_state, COUNT(*), COALESCE(SUM(version_size),0) "
                "FROM server_items GROUP BY pcs_state ORDER BY 2 DESC"), q)) {
            while (q.next()) {
                PcsBucket b;
                b.pcsState = q.value(0).toInt();
                b.count    = q.value(1).toInt();
                b.bytes    = q.value(2).toLongLong();
                switch (b.pcsState) {
                case 0:  b.label = QStringLiteral("空文件");     break;
                case 1:  b.label = QStringLiteral("有本地实体"); break;
                case 2:  b.label = QStringLiteral("仅云端");     break;
                case 4:  b.label = QStringLiteral("目录");       break;
                default: b.label = QStringLiteral("未知(%1)").arg(b.pcsState);
                }
                out.pcsDist.append(b);
            }
        }
    }

    // 6) 待处理项明细 (item_state<>0)
    {
        QSqlQuery q(db);
        // 真正的"待处理"必须排除 pcs_state=2(仅云端)。实测: item_state=1 且
        // pcs_state=2 的项, 本地磁盘上根本没有实体 —— 那是 iCloud Drive 的
        // 按需下载(Files On-Demand), 属**正常状态**, 不是待处理任务。
        // 之前不加这个条件, 界面上会一直挂着十几个"待处理项"误导用户。
        if (exec(QStringLiteral(
                "SELECT item_filename, item_type, COALESCE(version_size,0), "
                "       COALESCE(item_restore_path,'') "
                "FROM server_items WHERE item_state<>0 AND pcs_state<>2 "
                "ORDER BY version_size DESC LIMIT 20"), q)) {
            while (q.next()) {
                PendingItem it;
                it.name  = q.value(0).toString();
                it.isDir = (q.value(1).toInt() == 0);
                it.bytes = q.value(2).toLongLong();
                // item_restore_path 是相对同步根目录的路径, 拼绝对路径供"定位"用
                const QString rel = q.value(3).toString();
                if (!rel.isEmpty()) {
                    it.path = QDir::cleanPath(
                        QDir::fromNativeSeparators(paths::driveRoot()
                            + QLatin1Char('/') + rel));
                    // 文件已不在磁盘上 => 孤立记录(状态没清干净), 不是真在排队。
                    // 逐个 stat: 全表 LIMIT 20, 开销可忽略。
                    it.missing = !QFileInfo::exists(it.path);
                }
                out.pendingItems.append(it);
            }
        }
        // 仅云端(按需下载)的项数: 单独统计, 界面上标明它不算待处理
        QSqlQuery qc(db);
        if (exec(QStringLiteral(
                "SELECT COUNT(*) FROM server_items WHERE pcs_state=2"), qc)
            && qc.next()) {
            out.cloudOnlyCount = qc.value(0).toInt();
        }
        QSqlQuery qd(db);
        if (exec(QStringLiteral(
                "SELECT COUNT(*) FROM server_items "
                "WHERE item_state<>0 AND item_type=0 AND pcs_state<>2"), qd)
            && qd.next())
            out.pendingDirCount = qd.value(0).toInt();
    }

    // 7) 本地未提交项 (卡死判定的关键)
    {
        QSqlQuery q(db);
        if (exec(QStringLiteral(
                "SELECT item_filename, item_type, item_localsyncupstate, "
                "       COALESCE(version_size,0), COALESCE(item_in_flight_diffs,0), "
                "       COALESCE(item_local_diffs,0), item_absolutepath "
                "FROM local_items WHERE item_localsyncupstate<>0 "
                "   OR item_in_flight_diffs<>0 OR item_local_diffs<>0 "
                "LIMIT 20"), q)) {
            while (q.next()) {
                LocalPending p;
                p.name        = q.value(0).toString();
                p.syncUpState = q.value(2).toInt();
                p.bytes       = q.value(3).toLongLong();
                p.inFlight    = (q.value(4).toLongLong() != 0);
                p.hasDiff     = (q.value(5).toLongLong() != 0);
                p.absPath     = q.value(6).toString();
                out.localPendings.append(p);
            }
        }
    }

    // 7.5) 回收站 (删除的另一面)
    // item_restore_path 非空 = 该本地项已被删除、躺在 iCloud 回收站里,
    // 且记录了原始路径(可恢复)。这是"删除队列"唯一能拿到的"已完成"侧数据 ——
    // item_recursive_properties 里只有 needs_delete_doc_count(待删除),
    // 没有 deleted_count 这种已完成计数器。
    {
        QSqlQuery q(db);
        if (exec(QStringLiteral(
                "SELECT item_filename, COALESCE(item_restore_path,'') "
                "FROM local_items "
                "WHERE COALESCE(item_restore_path,'')<>'' "
                "ORDER BY item_filename LIMIT 30"), q)) {
            while (q.next()) {
                TrashItem t;
                t.name        = q.value(0).toString();
                t.restorePath = q.value(1).toString();
                out.trashItems.append(t);
            }
        }
        QSqlQuery qc(db);
        if (exec(QStringLiteral(
                "SELECT COUNT(*) FROM local_items "
                "WHERE COALESCE(item_restore_path,'')<>''"), qc)
            && qc.next()) {
            out.trashCount = qc.value(0).toInt();
        }
    }

    // 8) 活跃 zone
    {
        QSqlQuery q(db);
        if (exec(QStringLiteral(
                "SELECT zonename, sync_state, currentrequestid "
                "FROM server_zones WHERE currentrequestid <> ''"), q)) {
            while (q.next()) {
                ZoneInfo z;
                z.name      = q.value(0).toString();
                z.syncState = q.value(1).toInt();
                z.requestId = q.value(2).toString();
                out.activeZones.append(z);
            }
        }
        QSqlQuery qz(db);
        if (exec(QStringLiteral("SELECT COUNT(*) FROM server_zones"), qz)
            && qz.next())
            out.zoneTotal = qz.value(0).toInt();
    }

    // 9) 错误码
    {
        QSqlQuery q(db);
        if (exec(QStringLiteral(
                "SELECT upload_error_code, COUNT(*) FROM local_items "
                "WHERE upload_error_code <> 0 "
                "GROUP BY upload_error_code"), q)) {
            while (q.next())
                out.uploadErrors.append({ q.value(0).toInt(), q.value(1).toInt() });
        }
        QSqlQuery q2(db);
        if (exec(QStringLiteral(
                "SELECT download_error_code, COUNT(*) FROM local_items "
                "WHERE download_error_code <> 0 "
                "GROUP BY download_error_code"), q2)) {
            while (q2.next())
                out.downloadErrors.append({ q2.value(0).toInt(), q2.value(1).toInt() });
        }
    }

    // 一次性释放连接, 必须在所有 QSqlQuery 析构之后
    closeConnection(&db);
    return true;
}

// ── 活动指纹 ──────────────────────────────────────────────────────
// 绝对不能含时间戳/速率, 否则每帧都变, 停滞永远检测不出来。
QJsonObject IcloudMonitor::fingerprint(const Snapshot &s)
{
    QJsonObject fp;
    const QueueStat &q = s.queue;
    fp.insert(QStringLiteral("need_up_n"),   q.needUpCount);
    fp.insert(QStringLiteral("need_up_b"),   double(q.needUpBytes));
    fp.insert(QStringLiteral("need_dl_n"),   q.needDlCount);
    fp.insert(QStringLiteral("need_dl_b"),   double(q.needDlBytes));
    fp.insert(QStringLiteral("pending_n"),   q.pendingFetches);
    fp.insert(QStringLiteral("unstaged"),    q.dlUnstaged);
    fp.insert(QStringLiteral("synced_up"),   q.syncedUpCount);
    fp.insert(QStringLiteral("downloaded"),  q.downloadedCount);
    fp.insert(QStringLiteral("uploaded_b"),  double(q.uploadedBytes));
    fp.insert(QStringLiteral("downloaded_b"),double(q.downloadedBytes));
    fp.insert(QStringLiteral("faults"),      s.faults);
    fp.insert(QStringLiteral("over_quota"),  s.overQuota);
    fp.insert(QStringLiteral("rank"),        double(s.rankClient));
    fp.insert(QStringLiteral("unapplied"),   s.unapplied);

    // 本地未提交项: 名字 + 大小, 排序后保证稳定
    QJsonArray lp;
    QVector<QPair<QString, qint64>> tmp;
    for (const LocalPending &p : s.localPendings)
        tmp.append({ p.name, p.bytes });
    std::sort(tmp.begin(), tmp.end());
    for (const auto &t : tmp) {
        QJsonObject o;
        o.insert(QStringLiteral("n"), t.first);
        o.insert(QStringLiteral("b"), double(t.second));
        lp.append(o);
    }
    fp.insert(QStringLiteral("local_pending"), lp);
    return fp;
}

// ── 追加历史 ──────────────────────────────────────────────────────
int IcloudMonitor::appendHistory(const Snapshot &s)
{
    QJsonObject rec;
    rec.insert(QStringLiteral("ts"),
               QDateTime::currentDateTime().toString(
                   QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    rec.insert(QStringLiteral("epoch"), double(QDateTime::currentMSecsSinceEpoch() / 1000.0));
    rec.insert(QStringLiteral("busy"),  s.isBusy());
    rec.insert(QStringLiteral("state"), s.stateName());
    rec.insert(QStringLiteral("fp"),    fingerprint(s));

    QFile f(m_stateFile);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        // 首次运行文件可能不存在, 用 ReadWrite 保证创建
        if (!f.open(QIODevice::ReadWrite | QIODevice::Append | QIODevice::Text))
            return 0;
    }
    f.write(QJsonDocument(rec).toJson(QJsonDocument::Compact));
    f.write("\n");
    f.close();

    // 统计条数
    QFile rf(m_stateFile);
    int n = 0;
    if (rf.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!rf.atEnd()) {
            if (!rf.readLine().trimmed().isEmpty())
                ++n;
        }
    }
    // 超上限就裁头
    const int kMax = 2000;
    if (n > kMax) {
        QFile all(m_stateFile);
        if (all.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QList<QByteArray> lines;
            while (!all.atEnd()) {
                QByteArray ln = all.readLine();
                if (!ln.trimmed().isEmpty())
                    lines.append(ln);
            }
            all.close();
            QFile w(m_stateFile);
            if (w.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                for (int i = qMax(0, lines.size() - kMax); i < lines.size(); ++i)
                    w.write(lines.at(i));
            }
        }
        return kMax;
    }
    return n;
}

// ── 卡死判定 ──────────────────────────────────────────────────────
// 关键: 若历史里指纹从未变过, last_change 必须退化为观测起点,
//       否则停滞时长恒等于"距上次采样间隔", 永远小于阈值。
StallInfo IcloudMonitor::detectStall()
{
    StallInfo info;
    QFile f(m_stateFile);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return info;

    QList<QJsonObject> hist;
    while (!f.atEnd()) {
        const QByteArray ln = f.readLine().trimmed();
        if (ln.isEmpty())
            continue;
        const QJsonDocument d = QJsonDocument::fromJson(ln);
        if (d.isObject())
            hist.append(d.object());
    }
    if (hist.isEmpty())
        return info;

    const QJsonObject cur = hist.last();
    if (!cur.value(QStringLiteral("busy")).toBool()) {
        info.detail = QStringLiteral("空闲中(无任务), 不构成卡死");
        return info;                        // 空闲不算卡死
    }

    const QJsonObject curFp = cur.value(QStringLiteral("fp")).toObject();
    const double from = hist.first().value(QStringLiteral("epoch")).toDouble();
    double lastChange = -1.0;
    for (int i = hist.size() - 2; i >= 0; --i) {
        if (hist.at(i).value(QStringLiteral("fp")).toObject()
            != curFp) {
            lastChange = hist.at(i).value(QStringLiteral("epoch")).toDouble();
            break;
        }
    }
    QString note;
    if (lastChange < 0) {
        lastChange = from;
        note = QStringLiteral("(观测期内指纹从未变化)");
    }

    const double now = QDateTime::currentMSecsSinceEpoch() / 1000.0;
    info.seconds  = int(now - lastChange);
    info.observed = int(now - from);
    info.stalled  = (info.seconds > m_stallSeconds);

    QStringList d;
    const int upN = curFp.value(QStringLiteral("need_up_n")).toInt();
    const int dlN = curFp.value(QStringLiteral("need_dl_n")).toInt();
    const int pf  = curFp.value(QStringLiteral("pending_n")).toInt();
    if (upN) d << QStringLiteral("待上传 %1 个").arg(upN);
    if (dlN) d << QStringLiteral("待下载 %1 个").arg(dlN);
    if (pf)  d << QStringLiteral("取回队列 %1 条").arg(pf);
    const QJsonArray lp = curFp.value(QStringLiteral("local_pending")).toArray();
    if (!lp.isEmpty()) {
        QStringList names;
        for (int i = 0; i < lp.size() && i < 3; ++i)
            names << lp.at(i).toObject().value(QStringLiteral("n")).toString();
        d << QStringLiteral("本地未提交 %1 项 (%2)")
                 .arg(lp.size()).arg(names.join(QStringLiteral(", ")));
    }
    if (curFp.value(QStringLiteral("faults")).toInt() > 0)
        d << QStringLiteral("故障 %1").arg(curFp.value(QStringLiteral("faults")).toInt());
    info.detail = d.isEmpty()
        ? QStringLiteral("活动状态但无明确队列项")
        : d.join(QStringLiteral("; ")) + QLatin1Char(' ') + note;
    return info;
}

// ── 采样主流程 ────────────────────────────────────────────────────
// 线程分工(踩过的坑, 别改):
//   worker 线程: 只做 stageDatabase —— 复制 db+wal+shm 约 16MB, 是 IO 密集
//   主线程    : 执行所有 SQL。Qt 不允许在非主线程加载 SQL 驱动插件,
//               在 worker 里 addDatabase 会静默失效(所有 exec() 报
//               "Driver not loaded" 而 open() 却返回 true)。
//               16MB 的 SELECT 在本地 SSD 上是毫秒级, 放主线程不卡 UI。
void IcloudMonitor::doSample()
{
    QElapsedTimer timer;
    timer.start();

    QString dbPath;
    QString err;
    const bool staged = stageDatabase(dbPath, err);
    Snapshot s;
    s.dbOk = staged && readSnapshot(s, dbPath, err);
    s.dbError = err;
    s.elapsedMs = int(timer.elapsed());   // 含"复制三件套 + 读库"

    // 历史条数目前只用于落盘裁剪(appendHistory 内部自己做), 无需外传
    const qint64 tRead  = timer.elapsed();
    appendHistory(s);
    const qint64 tHist  = timer.elapsed();
    const StallInfo st = detectStall();
    const qint64 tTotal = timer.elapsed();

    // 采样全程跑在 GUI 线程上, 超过阈值就表现为界面/拖动卡顿。
    // 只在超阈值时记, 平时不刷屏; 分项便于定位是哪一段慢。
    if (tTotal > 100) {
        log::warn(QStringLiteral(
                      "[PERF] 采样偏慢 %1ms (复制+读库=%2 历史=%3 停滞检测=%4)")
                      .arg(tTotal)
                      .arg(tRead)
                      .arg(tHist - tRead)
                      .arg(tTotal - tHist));
    }

    // 异常判定
    QStringList anomalies;
    if (!s.dbOk) {
        anomalies << QStringLiteral("数据库不可读");
    } else {
        if (st.stalled)
            anomalies << QStringLiteral("卡死");
        if (s.faults > 0)
            anomalies << QStringLiteral("错误");
        if (s.overQuota > 0)
            anomalies << QStringLiteral("配额超限");
        if (!s.uploadErrors.isEmpty())
            anomalies << QStringLiteral("上传错误");
        if (!s.downloadErrors.isEmpty())
            anomalies << QStringLiteral("下载错误");
    }
    const bool anomalous = !anomalies.isEmpty();

    m_last = s;

    // ── 日志: 数据库可读性翻转(比状态变化更重要, 单独记) ──
    if (s.dbOk != m_lastDbOk) {
        if (s.dbOk) {
            log::info(QStringLiteral("[RUN] 数据库可读, 耗时 %1ms")
                          .arg(s.elapsedMs));
        } else {
            log::error(QStringLiteral("[RUN] 数据库不可读: %1").arg(s.dbError));
        }
        m_lastDbOk = s.dbOk;
    }

    // 已在 GUI 主线程(见 doSample 注释), 直接发信号即可,
    // 不需要再 invokeMethod 绕一圈。
    {
        const QJsonObject fp = fingerprint(s);
        const bool changed = (fp != m_lastFp);
        m_lastFp = fp;

        // ── 日志: 状态变化 ──
        // 只在状态名真的变了才记, 否则每5 秒一条噪音。
        // 队列数字的变化单独记一行 info, 便于事后回溯同步过程。
        const QString now = s.stateName();
        if (now != m_lastStateName) {
            log::syncChanged(m_lastStateName, now,
                             QStringLiteral("队列 up=%1/%2 dl=%3/%4 删=%5/%6 "
                                            "取回=%7 故障=%8")
                                 .arg(s.queue.syncedUpCount)
                                 .arg(s.queue.needUpCount)
                                 .arg(s.queue.downloadedCount)
                                 .arg(s.queue.needDlCount)
                                 // 删=待删除/回收站(后者是"已完成"侧)
                                 .arg(s.queue.needDelCount)
                                 .arg(s.trashCount)
                                 .arg(s.queue.pendingFetches)
                                 .arg(s.faults));
            m_lastStateName = now;
        } else if (changed) {
            log::info(QStringLiteral("[SYNC] 队列变化 up=%1/%2 dl=%3/%4 "
                                     "删=%5/%6 取回=%7")
                          .arg(s.queue.syncedUpCount)
                          .arg(s.queue.needUpCount)
                          .arg(s.queue.downloadedCount)
                          .arg(s.queue.needDlCount)
                          .arg(s.queue.needDelCount)
                          .arg(s.trashCount)
                          .arg(s.queue.pendingFetches));
        }

        emit snapshotReady(s);
        if (changed)
            emit stateChanged(s);

        if (anomalous) {
            if (!s.dbOk) {
                log::error(QStringLiteral("[ANOMALY] 数据库不可读: %1")
                               .arg(s.dbError));
                emit anomalyDetected(QStringLiteral("严重"),
                                     QStringLiteral("数据库不可读"),
                                     s.dbError);
            }
            if (st.stalled) {
                log::error(QStringLiteral("[ANOMALY] 卡死: 停滞 %1 秒 (阈值 %2) | %3")
                               .arg(st.seconds).arg(m_stallSeconds)
                               .arg(st.detail));
                emit anomalyDetected(
                    QStringLiteral("卡死"),
                    QStringLiteral("活动状态停滞 %1 秒 (阈值 %2 秒)")
                        .arg(st.seconds).arg(m_stallSeconds),
                    st.detail);
            }
            if (s.faults > 0) {
                log::warn(QStringLiteral("[ANOMALY] 故障项 %1 个").arg(s.faults));
                emit anomalyDetected(QStringLiteral("错误"),
                                     QStringLiteral("故障项 %1 个").arg(s.faults),
                                     QStringLiteral("详见错误面板"));
            }
            if (s.overQuota > 0) {
                log::error(QStringLiteral("[ANOMALY] 超配额 %1 个").arg(s.overQuota));
                emit anomalyDetected(QStringLiteral("配额"),
                                     QStringLiteral("超配额 %1 个").arg(s.overQuota),
                                     QStringLiteral("iCloud 云空间已满"));
            }
            if (!s.uploadErrors.isEmpty()) {
                log::error(QStringLiteral("[ANOMALY] 上传错误码 %1 个")
                               .arg(s.uploadErrors.size()));
                emit anomalyDetected(QStringLiteral("上传错误"),
                                     QStringLiteral("存在上传错误码"), QString());
            }
            if (!s.downloadErrors.isEmpty()) {
                log::error(QStringLiteral("[ANOMALY] 下载错误码 %1 个")
                               .arg(s.downloadErrors.size()));
                emit anomalyDetected(QStringLiteral("下载错误"),
                                     QStringLiteral("存在下载错误码"), QString());
            }
            m_lastAnomalous = true;
        } else if (m_lastAnomalous) {
            m_lastAnomalous = false;
            log::info(QStringLiteral("[ANOMALY] 已恢复, 同步回到正常"));
            emit anomalyCleared();
        }
    }
}

}  // namespace icloud
