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

    // ── 错误 ──
    Q_PROPERTY(int     faults    READ faults    NOTIFY snapshotChanged)
    Q_PROPERTY(int     overQuota READ overQuota NOTIFY snapshotChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY snapshotChanged)

    // ── 同步游标 ──
    Q_PROPERTY(QString rankText READ rankText NOTIFY snapshotChanged)

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

    // ── 日志 ──
    Q_PROPERTY(QString logDir READ logDir CONSTANT)
    Q_PROPERTY(QString logFile READ logFile NOTIFY logChanged)
    // 在资源管理器中打开日志目录
    Q_INVOKABLE void openLogDir();

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
    int     upCount() const { return m_snap.queue.needUpCount; }
    QString upSize() const;
    bool    upBusy() const { return m_snap.queue.needUpCount > 0; }

    int     dlDone() const;
    int     dlTotal() const;
    int     dlCount() const { return m_snap.queue.needDlCount; }
    QString dlSize() const;
    int     dlPending() const { return m_snap.queue.pendingFetches; }
    bool    dlBusy() const;

    int     faults() const { return m_snap.faults; }
    int     overQuota() const { return m_snap.overQuota; }
    QString lastError() const;
    QString rankText() const;

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

    PendingModel       *pendingItems() { return &m_pendingModel; }
    LocalPendingModel  *localPendings() { return &m_localModel; }

    void setRefreshSec(int s);
    void setStallSec(int s);

    // 日志路径(供设置页展示)
    QString logDir() const { return log::logDir(); }
    QString logFile() const { return log::currentLogFile(); }
    // 同步根目录(可被用户改, 所以每次读都回 IcloudPaths 取当前值)
    QString driveRoot() const;

signals:
    void snapshotChanged();
    void icloudStateChanged();
    void autoStartChanged();
    void settingsChanged();
    void anomalyChanged();
    void logChanged();
    void driveRootChanged();
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
