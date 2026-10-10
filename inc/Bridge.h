// ======================================================================
//  Bridge.h  -  QML <-> C++ 桥接层
//
//  职责: 把 Snapshot 拍平成 QML 可直接绑定的属性/模型。
//  QML 不做数据处理, 只负责显示 —— 保持"单一真相源"在 C++ 侧。
// ======================================================================
#ifndef BRIDGE_H
#define BRIDGE_H

#include <QAbstractListModel>
#include <QJsonObject>
#include <QObject>
#include <QQmlEngine>
#include <QTimer>
#include <QUrl>

#include "AutoStart.h"
#include "IcloudCategories.h"
#include "IcloudMonitor.h"
#include "IcloudService.h"
#include "IcloudSnapshot.h"
#include "Log.h"

namespace icloud {

// ── 待处理项列表模型 ──────────────────────────────────────────────
class PendingModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        SizeRole,
        IsDirRole,
        SizeTextRole,
        PathRole,
        MissingRole,
    };

    explicit PendingModel(QObject *parent = nullptr)
        : QAbstractListModel(parent) {}

    void setItems(const QVector<PendingItem> &v)
    {
        beginResetModel();
        m_items = v;
        endResetModel();
        emit countChanged();
    }

    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : m_items.size();
    }

    QVariant data(const QModelIndex &idx, int role) const override
    {
        if (!idx.isValid() || idx.row() < 0 || idx.row() >= m_items.size())
            return {};
        const PendingItem &it = m_items.at(idx.row());
        switch (role) {
        case NameRole:     return it.name;
        case SizeRole:     return it.bytes;
        case IsDirRole:    return it.isDir;
        case SizeTextRole: return humanSize(it.bytes);
        case PathRole:     return it.path;
        case MissingRole:  return it.missing;
        }
        return {};
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return {
            { NameRole,     "name" },
            { SizeRole,     "size" },
            { IsDirRole,    "isDir" },
            { SizeTextRole, "sizeText" },
            { PathRole,     "path" },
            { MissingRole,  "missing" },
        };
    }

    static QString humanSize(qint64 b)
    {
        const char *u[] = { "B", "KB", "MB", "GB", "TB" };
        double v = double(b);
        int i = 0;
        while (v >= 1024.0 && i < 4) { v /= 1024.0; ++i; }
        return QStringLiteral("%1 %2").arg(v, 0, 'f', 1).arg(QLatin1String(u[i]));
    }

signals:
    void countChanged();

private:
    QVector<PendingItem> m_items;
};

// ── 本地未提交项模型 (卡死源头, 要能直接看到路径) ─────────────────
class LocalPendingModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        BytesRole,
        StateTextRole,
        PathRole,
    };

    explicit LocalPendingModel(QObject *parent = nullptr)
        : QAbstractListModel(parent) {}

    void setItems(const QVector<LocalPending> &v)
    {
        beginResetModel();
        m_items = v;
        endResetModel();
        emit countChanged();
    }

    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : m_items.size();
    }

    QVariant data(const QModelIndex &idx, int role) const override
    {
        if (!idx.isValid() || idx.row() < 0 || idx.row() >= m_items.size())
            return {};
        const LocalPending &p = m_items.at(idx.row());
        switch (role) {
        case NameRole:  return p.name;
        case BytesRole: return p.bytes;
        case StateTextRole: {
            QStringList t;
            if (p.syncUpState) t << QStringLiteral("syncup=%1").arg(p.syncUpState);
            if (p.inFlight)    t << QStringLiteral("inflight");
            if (p.hasDiff)     t << QStringLiteral("diff");
            return t.join(QStringLiteral(", "));
        }
        case PathRole:  return p.absPath;
        }
        return {};
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return {
            { NameRole,  "name" },
            { BytesRole, "bytes" },
            { StateTextRole, "stateText" },
            { PathRole,  "path" },
        };
    }

signals:
    void countChanged();

private:
    QVector<LocalPending> m_items;
};

// ── 回收站项模型 ──────────────────────────────────────────────────
// 删除的另一面: 已删除但可恢复的项, 带原始路径。
class TrashModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        RestorePathRole,
    };

    explicit TrashModel(QObject *parent = nullptr)
        : QAbstractListModel(parent) {}

    void setItems(const QVector<TrashItem> &v)
    {
        beginResetModel();
        m_items = v;
        endResetModel();
        emit countChanged();
    }

    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : m_items.size();
    }

    QVariant data(const QModelIndex &idx, int role) const override
    {
        if (!idx.isValid() || idx.row() < 0 || idx.row() >= m_items.size())
            return {};
        const TrashItem &t = m_items.at(idx.row());
        switch (role) {
        case NameRole:        return t.name;
        case RestorePathRole: return t.restorePath;
        }
        return {};
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return {
            { NameRole,        "name" },
            { RestorePathRole, "restorePath" },
        };
    }

signals:
    void countChanged();

private:
    QVector<TrashItem> m_items;
};

// ── 主桥接对象 ────────────────────────────────────────────────────
class Bridge : public QObject
{
    Q_OBJECT

    // ── 主状态 ──
    Q_PROPERTY(QString stateName      READ stateName      NOTIFY snapshotChanged)
    Q_PROPERTY(QString stateDetail    READ stateDetail    NOTIFY snapshotChanged)
    Q_PROPERTY(bool    busy           READ busy           NOTIFY snapshotChanged)
    Q_PROPERTY(bool    inSync         READ inSync         NOTIFY snapshotChanged)
    Q_PROPERTY(bool    dbOk           READ dbOk           NOTIFY snapshotChanged)
    Q_PROPERTY(QString clientVersion  READ clientVersion  NOTIFY snapshotChanged)
    Q_PROPERTY(QString timestamp      READ timestamp      NOTIFY snapshotChanged)

    // ── 上传 ──
    Q_PROPERTY(int     upDone   READ upDone   NOTIFY snapshotChanged)
    Q_PROPERTY(int     upTotal  READ upTotal  NOTIFY snapshotChanged)
    Q_PROPERTY(int     upCount  READ upCount  NOTIFY snapshotChanged)
    Q_PROPERTY(QString upSize   READ upSize   NOTIFY snapshotChanged)
    Q_PROPERTY(bool    upBusy   READ upBusy   NOTIFY snapshotChanged)

    // ── 下载 ──
    Q_PROPERTY(int     dlDone   READ dlDone   NOTIFY snapshotChanged)
    Q_PROPERTY(int     dlTotal  READ dlTotal  NOTIFY snapshotChanged)
    Q_PROPERTY(int     dlCount  READ dlCount  NOTIFY snapshotChanged)
    Q_PROPERTY(QString dlSize   READ dlSize   NOTIFY snapshotChanged)
    Q_PROPERTY(int     dlPending READ dlPending NOTIFY snapshotChanged)
    Q_PROPERTY(bool    dlBusy   READ dlBusy   NOTIFY snapshotChanged)

    // ── 删除 ──
    // 待删除 = item_recursive_properties.needs_delete_doc_count
    // 已完成 = 回收站项数(local_items.item_restore_path 非空)
    // 表里没有 deleted_count, 所以"完成"侧用回收站项数对应, 两者都是真实数据。
    Q_PROPERTY(int     delDone  READ delDone  NOTIFY snapshotChanged)
    Q_PROPERTY(int     delTotal READ delTotal NOTIFY snapshotChanged)
    Q_PROPERTY(int     delCount READ delCount NOTIFY snapshotChanged)
    Q_PROPERTY(bool    delBusy  READ delBusy  NOTIFY snapshotChanged)

    // ── 错误 ──
    Q_PROPERTY(int     faults    READ faults    NOTIFY snapshotChanged)
    Q_PROPERTY(int     overQuota READ overQuota NOTIFY snapshotChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY snapshotChanged)

    // ── 同步游标 ──
    Q_PROPERTY(QString rankText READ rankText NOTIFY snapshotChanged)

    // 仅云端项数(pcs_state=2, 按需下载)。不算待处理任务, 单独展示。
    Q_PROPERTY(int cloudOnlyCount READ cloudOnlyCount NOTIFY snapshotChanged)
    // 真正的待处理项数(有本地实体且状态非 0)
    Q_PROPERTY(int pendingRealCount READ pendingRealCount NOTIFY snapshotChanged)
    // 文件已删但状态未清的孤立记录数(不占队列, 仅提示)
    Q_PROPERTY(int pendingOrphanCount READ pendingOrphanCount NOTIFY snapshotChanged)
    // 本地未提交项真实总数(模型只展示前几条, 这里给总数)
    Q_PROPERTY(int localPendingTotal READ localPendingTotal NOTIFY snapshotChanged)

    // ── iCloud 进程 ──
    Q_PROPERTY(bool    icloudRunning READ icloudRunning NOTIFY icloudStateChanged)
    Q_PROPERTY(QString icloudProcesses READ icloudProcesses NOTIFY icloudStateChanged)
    Q_PROPERTY(bool    busyRestarting READ busyRestarting NOTIFY icloudStateChanged)

    // ── 设置 ──
    Q_PROPERTY(bool    autoStart READ autoStart NOTIFY autoStartChanged)
    Q_PROPERTY(int     refreshSec READ refreshSec WRITE setRefreshSec
                         NOTIFY settingsChanged)
    Q_PROPERTY(int     stallSec READ stallSec WRITE setStallSec
                         NOTIFY settingsChanged)

    // ── 异常 ──
    Q_PROPERTY(bool    anomalous READ anomalous NOTIFY anomalyChanged)
    Q_PROPERTY(QString anomalyTitle READ anomalyTitle NOTIFY anomalyChanged)
    Q_PROPERTY(QString anomalyDetail READ anomalyDetail NOTIFY anomalyChanged)
    Q_PROPERTY(QStringList anomalyList READ anomalyList NOTIFY anomalyChanged)

    // ── 云端状态分布 (给 PcsPanel 用) ──
    Q_PROPERTY(QVariantList pcsBuckets READ pcsBuckets NOTIFY snapshotChanged)

    // ── 模型 ──
    Q_PROPERTY(icloud::PendingModel *pendingItems
               READ pendingItems CONSTANT)
    Q_PROPERTY(icloud::LocalPendingModel *localPendings
               READ localPendings CONSTANT)
    Q_PROPERTY(icloud::TrashModel *trashItems
               READ trashItems CONSTANT)

public:
    explicit Bridge(QObject *parent = nullptr);
    ~Bridge() override;

    // ── 启动/停止监控 ──
    Q_INVOKABLE void startMonitoring();
    Q_INVOKABLE void stopMonitoring();
    Q_INVOKABLE void refreshNow();

    // ── iCloud 控制 ──
    Q_INVOKABLE void restartIcloud();
    Q_INVOKABLE void stopIcloud();
    Q_INVOKABLE void startIcloud();

    // ── 自启动 ──
    Q_INVOKABLE bool setAutoStart(bool on);

    // ── 历史 ──
    Q_INVOKABLE void resetHistory();
    Q_INVOKABLE QString stateFilePath() const;

    // 在资源管理器中定位一个文件/目录(待处理项、本地未提交项、回收站项点"定位")。
    // 文件 -> explorer /select 选中它; 目录 -> 直接打开。
    // 路径为空或整条链都不存在时优雅降级(记日志 + 弹提示), 不崩。
    Q_INVOKABLE void openItemLocation(const QString &path);

    // 回收站项专用: 传 item_restore_path(文件被删除前的原始路径)。
    // ★ 与 openItemLocation 的区别: 回收站项的文件**已经被删除**, 原路径下
    //   已经没有该文件了, 所以不做 /select 选中文件, 而是打开它原来所在的
    //   目录, 让用户看到上下文并决定是否恢复; 提示语也据实说明"已删除"。
    Q_INVOKABLE void openTrashLocation(const QString &originalPath);

    // 用系统默认浏览器打开一个 URL(设置页的仓库链接等)。
    // 走 QDesktopServices 而不是 QProcess: 后者要区分 http/浏览器命令行, 且
    // 在中文系统上易因编码起不来。
    Q_INVOKABLE void openUrl(const QString &url);

    // ── 日志 ──
    Q_PROPERTY(QString logDir READ logDir CONSTANT)
    Q_PROPERTY(QString logFile READ logFile NOTIFY logChanged)
    // 在资源管理器中打开日志目录
    Q_INVOKABLE void openLogDir();

    // 设置页"测试通知": 验证系统通知到底能不能弹出来
    Q_INVOKABLE void testSystemNotification();

    // ── 同步根目录 ──
    // 注意: 不能只靠 main.cpp 注入的 DriveRoot 上下文属性 ——
    // 那是启动时的快照, 用户改了之后不会变。必须走这个属性。
    Q_PROPERTY(QString driveRoot READ driveRoot NOTIFY driveRootChanged)
    // 在资源管理器中打开 iCloud Drive 同步根目录
    Q_INVOKABLE void openDriveRoot();
    // 设置同步根目录 (写入注册表), 失败返回 false
    Q_INVOKABLE bool setDriveRoot(const QString &path);
    // 恢复内置默认路径
    Q_INVOKABLE void resetDriveRoot();

    // ── 四类同步 (云盘/相册/密码/书签) ──
    // 每个元素是 map: key name summary enabled userSet autoOn autoWhy
    //                 hasData note details[{label,value}]
    Q_PROPERTY(QVariantList categories READ categories NOTIFY categoriesChanged)
    // 手动指定某类是否启用(覆盖自动检测)。自动检测对密码/书签不可靠,
    // 详见 IcloudCategories.h。
    Q_INVOKABLE void setCategoryEnabled(const QString &key, bool on);
    // 清除手动设置, 回退到自动检测
    Q_INVOKABLE void clearCategoryEnabled(const QString &key);

    // ── 侧边栏顺序 / 默认启动页 ──
    // 首页固定最前、设置固定最后, 中间四类可调序。
    Q_PROPERTY(QStringList navOrder READ navOrder NOTIFY navOrderChanged)
    Q_INVOKABLE void moveCategory(const QString &key, int delta);
    // 拖拽排序: 把 key 移到 toIndex 位置(供 ListView 拖放调用)。
    // 与 moveCategory 的区别: 后者是"上移/下移一格", 这个是"移到指定位",
    // 拖动一次可能跨多项。
    Q_INVOKABLE void moveCategoryTo(const QString &key, int toIndex);
    Q_INVOKABLE void resetNavOrder();
    // 显示名(供顺序设置界面显示"云盘"而不是"drive")
    Q_INVOKABLE QString categoryName(const QString &key) const;
    Q_PROPERTY(QString defaultPage READ defaultPage WRITE setDefaultPage
                   NOTIFY settingsChanged)

    // ── getter ──
    QString stateName() const;
    QString stateDetail() const;
    bool    busy() const;
    bool    inSync() const;
    bool    dbOk() const;
    QString clientVersion() const;
    QString timestamp() const;

    int     upDone() const;
    int     upTotal() const;
    // 待上传数。
    // ★不能只读 item_recursive_properties.needs_upload_count —— 实测该计数
    //   在本机长期为 0/滞后(上传文件时面板纹丝不动), 不可作为实时信号。
    //   真正实时的是 local_items 里"有本地未提交改动"的项(用户刚存盘的文件
    //   立刻出现在那里)。两者取较大值: 哪个先动都算数, 又不会重复计数。
    int     upCount() const
    {
        int n = m_snap.queue.needUpCount;
        int localUp = 0;
        for (const LocalPending &p : m_snap.localPendings)
            if (p.syncUpState || p.inFlight || p.hasDiff)
                ++localUp;
        return qMax(n, localUp);
    }
    QString upSize() const;
    bool    upBusy() const { return upCount() > 0; }

    int     dlDone() const;
    int     dlTotal() const;
    int     dlCount() const { return m_snap.queue.needDlCount; }
    QString dlSize() const;
    int     dlPending() const { return m_snap.queue.pendingFetches; }
    bool    dlBusy() const;

    int     delDone() const  { return m_snap.trashCount; }
    int     delCount() const { return m_snap.queue.needDelCount; }
    int     delTotal() const { return m_snap.trashCount + m_snap.queue.needDelCount; }
    bool    delBusy() const  { return m_snap.queue.needDelCount > 0; }

    int     faults() const { return m_snap.faults; }
    int     overQuota() const { return m_snap.overQuota; }
    QString lastError() const;
    QString rankText() const;
    int     cloudOnlyCount() const { return m_snap.cloudOnlyCount; }
    // 真·待处理项数: 只算文件确实还在磁盘上的。文件已被删、但 iCloud 状态
    // 没清干净的(孤立记录)不计入 —— 它们并不占传输队列(need_* 全是 0),
    // 计入 —— 它们并不占传输队列(need_* 全是 0),
    // 计入只会让人误以为同步卡住。
    int     pendingRealCount() const
    {
        int n = m_snap.pendingDirCount;
        for (const PendingItem &it : m_snap.pendingItems)
            if (!it.missing)
                ++n;
        return n;
    }
    // 孤立记录数(文件已删、状态未清), 界面单独标注, 不算待处理
    int     pendingOrphanCount() const
    {
        int n = 0;
        for (const PendingItem &it : m_snap.pendingItems)
            if (it.missing)
                ++n;
        return n;
    }
    // 本地未提交项真实总数
    int     localPendingTotal() const { return m_snap.localPendingTotal; }

    bool    icloudRunning() const { return m_icloudRunning; }
    QString icloudProcesses() const { return m_procList.join(QStringLiteral(", ")); }
    bool    busyRestarting() const { return m_restarting; }

    bool    autoStart() const;
    int     refreshSec() const { return m_refreshSec; }
    int     stallSec() const { return m_monitor->stallSeconds(); }

    bool    anomalous() const { return m_anomalous; }
    QString anomalyTitle() const { return m_anomalyTitle; }
    QString anomalyDetail() const { return m_anomalyDetail; }
    QStringList anomalyList() const { return m_anomalyList; }
    QVariantList pcsBuckets() const;

    // 四类状态(每次采样刷新)
    QVector<CategoryStatus> m_categories;
    QStringList m_navOrder;
    QString     m_defaultPage;
    // 类别刷新计数: 三类外部库较重, 每 3 个采样才刷一次
    int         m_catTick = 0;
    // 上一次的四类状态签名, 只在变化时记日志
    QString     m_lastCatSig;
    void refreshCategories();
    // 从 QSettings 读回顺序与默认页; 顺带剔除已不存在的 key
    void loadNavSettings();
    void saveNavOrder();

    PendingModel       *pendingItems() { return &m_pendingModel; }
    LocalPendingModel  *localPendings() { return &m_localModel; }
    TrashModel         *trashItems() { return &m_trashModel; }

    void setRefreshSec(int s);
    void setStallSec(int s);

    // 日志路径(供设置页展示)
    QString logDir() const { return log::logDir(); }
    QString logFile() const { return log::currentLogFile(); }
    // 同步根目录(可被用户改, 所以每次读都回 IcloudPaths 取当前值)
    QString driveRoot() const;

    QVariantList categories() const;
    QStringList  navOrder() const { return m_navOrder; }
    QString      defaultPage() const { return m_defaultPage; }
    void         setDefaultPage(const QString &p);

signals:
    void snapshotChanged();
    void icloudStateChanged();
    void autoStartChanged();
    void settingsChanged();
    void anomalyChanged();
    void logChanged();
    void driveRootChanged();
    void categoriesChanged();
    void navOrderChanged();
    // 请求 shell 弹 Windows 系统通知(由 main.cpp 的托盘承接)。
    // 两个来源: ①刚进入卡死 ②设置页的"测试通知"按钮。
    void systemNotify(const QString &title, const QString &detail);
    // 给 QML 弹提示用
    void notify(const QString &kind, const QString &text);

private slots:
    void onSnapshot(const icloud::Snapshot &s);
    void onStateChanged(const icloud::Snapshot &s);
    void onAnomaly(const QString &kind, const QString &title,
                   const QString &detail);
    void onAnomalyCleared();
    void onIcloudFinished(bool ok, const QString &msg);
    void onIcloudProgress(const QString &msg);
    void onProcTimer();

private:
    IcloudMonitor  *m_monitor;
    IcloudService  *m_service;
    AutoStart      *m_autoStart;

    Snapshot m_snap;
    PendingModel      m_pendingModel;
    LocalPendingModel m_localModel;
    TrashModel        m_trashModel;

    QTimer *m_procTimer = nullptr;
    bool    m_icloudRunning = false;
    bool    m_restarting = false;
    QStringList m_procList;

    bool    m_anomalous = false;
    QString m_anomalyTitle;
    QString m_anomalyDetail;
    QStringList m_anomalyList;

    int     m_refreshSec = 5;
    int     m_stallSec   = 300;   // 卡死阈值(秒), 需与 m_monitor 同步
};

}  // namespace icloud

#endif  // BRIDGE_H
