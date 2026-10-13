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
#include "IcloudService.h"
#include "IcloudSnapshot.h"   // 仅用其中的 PendingItem/LocalPending/TrashItem 结构
#include "SidecarClient.h"
// #include "IcloudQuota.h"   // 存储卡片暂缓, 见下方 quota 属性段说明
#include "Log.h"

namespace icloud {

// ── payload 取值小工具 ────────────────────────────────────────────
// 放在头里是因为类内有若干 inline getter 也要读 payload(避免每个都写一遍
// "value(...).toInt()")。缺字段一律回落默认值 —— 旁路离线时 payload 为空对象,
// 界面显示 0 而不是旧数据, 这正是我们要的行为。
inline int jint(const QJsonObject &o, const char *k)
{
    return o.value(QLatin1String(k)).toInt();
}
inline bool jbool(const QJsonObject &o, const char *k, bool def = false)
{
    const QJsonValue v = o.value(QLatin1String(k));
    return v.isBool() ? v.toBool() : def;
}
inline QString jstr(const QJsonObject &o, const char *k)
{
    return o.value(QLatin1String(k)).toString();
}

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

    // ── 监控旁路状态 ──
    // ★ 旁路离线时**必须**停止沿用旧数据(取数与判定在旁路里, 它挂了数据就是
    //   不可信的)。离线期间状态区显示"监控不可用", 数字一律归零, 不再假装正常。
    Q_PROPERTY(bool    sidecarOnline READ sidecarOnline NOTIFY sidecarStateChanged)
    Q_PROPERTY(QString sidecarOfflineReason READ sidecarOfflineReason
                   NOTIFY sidecarStateChanged)

    // ── 设置 ──
    Q_PROPERTY(bool    autoStart READ autoStart NOTIFY autoStartChanged)
    Q_PROPERTY(int     refreshSec READ refreshSec WRITE setRefreshSec
                         NOTIFY settingsChanged)
    Q_PROPERTY(int     stallSec READ stallSec WRITE setStallSec
                         NOTIFY settingsChanged)
    // 标题栏关闭按钮的行为: true = 关闭窗口但进程留在后台常驻(托盘),
    // false = 关闭即退出程序。由 Main.qml 的 onClosing 读取。
    Q_PROPERTY(bool    closeToTray READ closeToTray WRITE setCloseToTray
                         NOTIFY settingsChanged)

    // ── 异常 ──
    Q_PROPERTY(bool    anomalous READ anomalous NOTIFY anomalyChanged)
    Q_PROPERTY(QString anomalyTitle READ anomalyTitle NOTIFY anomalyChanged)
    Q_PROPERTY(QString anomalyDetail READ anomalyDetail NOTIFY anomalyChanged)
    Q_PROPERTY(QStringList anomalyList READ anomalyList NOTIFY anomalyChanged)

    // ── 云端状态分布 (给 PcsPanel 用) ──
    Q_PROPERTY(QVariantList pcsBuckets READ pcsBuckets NOTIFY snapshotChanged)

    // ── iCloud 存储配额(首页存储卡片) ──
// ★ 整段暂不启用: 数据层代码(IcloudQuota/BPlist)已写好并提交, 但自研的
//   二进制 plist 解析器还没调通 —— Apple 这个偏好文件不是标准 bplist
//   (整数用 0x1_ 标记、bool 用 0x08/0x09, 与教科书编码不同)。
//   解析器验证通过后, 取消下面这段与 CMakeLists 里的注释即可接上首页。
//    Q_PROPERTY(bool       quotaOk        READ quotaOk        NOTIFY quotaChanged)
//    Q_PROPERTY(QString    quotaError     READ quotaError     NOTIFY quotaChanged)
//    Q_PROPERTY(QString    quotaTotalText READ quotaTotalText NOTIFY quotaChanged)
//    Q_PROPERTY(QString    quotaUsedText  READ quotaUsedText  NOTIFY quotaChanged)
//    Q_PROPERTY(QString    quotaFreeText  READ quotaFreeText  NOTIFY quotaChanged)
//    Q_PROPERTY(double     quotaUsedRatio READ quotaUsedRatio NOTIFY quotaChanged)
//    Q_PROPERTY(bool       quotaOver      READ quotaOver      NOTIFY quotaChanged)
    // 分类用量: [{label, bytes, bytesText, color, ratio}, ...]
//    Q_PROPERTY(QVariantList quotaBuckets  READ quotaBuckets  NOTIFY quotaChanged)
    // Manage 按钮: 打开 Apple 的"管理 iCloud 储存空间"页面
//    Q_INVOKABLE void openQuotaManage();

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
    // 值优先取旁路下发的 driveRoot(它按 env>注册表>数据库推导 解析,
    // 见 sidecar/icloudmon/snapshot.py 的 current_drive_root), 旁路离线
    // 时退回 C++ 侧 IcloudPaths 的兜底(driveRootIsCustom 判断同理)。
    Q_PROPERTY(QString driveRoot READ driveRoot NOTIFY driveRootChanged)
    // 用户是否显式覆盖过根目录(注册表里有 driveRoot)。设置页的「默认」
    // 按钮据此显隐 —— 以前是拿路径去比硬编码字符串, 硬编码已删。
    Q_PROPERTY(bool driveRootIsCustom READ driveRootIsCustom NOTIFY driveRootChanged)
    // 在资源管理器中打开 iCloud Drive 同步根目录
    Q_INVOKABLE void openDriveRoot();
    // 设置同步根目录 (写入注册表), 失败返回 false
    Q_INVOKABLE bool setDriveRoot(const QString &path);
    // 恢复默认路径(清掉注册表覆盖值, 回到数据库推导/标准安装位置)
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
    // 打开 iCloud 回收站目录(<同步根>/.Trash) 与系统回收站。
    // 这是**看**, 不是改 —— 删除动作一律不在本程序里做。
    Q_INVOKABLE void openCloudTrash();
    Q_INVOKABLE void openSystemTrash();
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
    // 待上传数由旁路给出(它已经把"队列计数"与"本地未提交项"取较大值处理好,
    // 见 sidecar/bridge.py —— needs_upload_count 实测长期为 0/滞后)。
    int     upCount() const { return jint(m_payload, "upCount"); }
    QString upSize() const;
    bool    upBusy() const { return jbool(m_payload, "upBusy"); }

    int     dlDone() const;
    int     dlTotal() const;
    int     dlCount() const { return jint(m_payload, "dlCount"); }
    QString dlSize() const;
    int     dlPending() const { return jint(m_payload, "dlPending"); }
    bool    dlBusy() const;

    int     delDone() const  { return jint(m_payload, "delDone"); }
    int     delCount() const { return jint(m_payload, "delCount"); }
    int     delTotal() const { return jint(m_payload, "delTotal"); }
    bool    delBusy() const  { return jbool(m_payload, "delBusy"); }

    int     faults() const { return jint(m_payload, "faults"); }
    int     overQuota() const { return jint(m_payload, "overQuota"); }
    QString lastError() const;
    QString rankText() const;
    int     cloudOnlyCount() const { return jint(m_payload, "cloudOnlyCount"); }
    // 真·待处理项数 / 孤立记录数 / 本地未提交总数: 均由旁路算好
    // (只算文件确实还在磁盘上的; 文件已删但状态没清的不计入待处理)
    int     pendingRealCount() const { return jint(m_payload, "pendingRealCount"); }
    int     pendingOrphanCount() const { return jint(m_payload, "pendingOrphanCount"); }
    int     localPendingTotal() const { return jint(m_payload, "localPendingTotal"); }

    bool    icloudRunning() const { return m_icloudRunning; }
    QString icloudProcesses() const { return m_procList.join(QStringLiteral(", ")); }
    bool    busyRestarting() const { return m_restarting; }

    bool    sidecarOnline() const { return m_sidecarOnline; }
    QString sidecarOfflineReason() const { return m_sidecarOfflineReason; }

    bool    autoStart() const;
    int     refreshSec() const { return m_refreshSec; }
    // 卡死阈值: 判定在旁路侧(sidecar/stall.py), 这里只保留一份数值用于展示
    // 与下发 —— 不再从 IcloudMonitor 读。
    int     stallSec() const { return m_stallSec; }
    bool    closeToTray() const { return m_closeToTray; }

    bool    anomalous() const { return m_anomalous; }
    QString anomalyTitle() const { return m_anomalyTitle; }
    QString anomalyDetail() const { return m_anomalyDetail; }
    QStringList anomalyList() const { return m_anomalyList; }
    QVariantList pcsBuckets() const;

    // ── iCloud 存储配额 getter ──(暂缓, 见上方 Q_PROPERTY 段说明)
    // bool        quotaOk()        const;
    // QString     quotaError()     const;
    // QString     quotaTotalText() const;
    // QString     quotaUsedText()  const;
    // QString     quotaFreeText()  const;
    // double      quotaUsedRatio() const;
    // bool        quotaOver()      const;
    // QVariantList quotaBuckets()  const;
    // void        refreshQuota();

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
    // 从 QSettings 读回窗口行为类设置(关闭按钮行为等)
    void loadUiSettings();

    PendingModel       *pendingItems() { return &m_pendingModel; }
    LocalPendingModel  *localPendings() { return &m_localModel; }
    TrashModel         *trashItems() { return &m_trashModel; }

    void setRefreshSec(int s);
    void setStallSec(int s);
    void setCloseToTray(bool on);

    // 日志路径(供设置页展示)
    QString logDir() const { return log::logDir(); }
    QString logFile() const { return log::currentLogFile(); }
    // 同步根目录(可被用户改, 所以每次读都取当前值: 先旁路 payload, 后兜底)
    QString driveRoot() const;
    // 用户是否显式覆盖过根目录
    bool    driveRootIsCustom() const;

    QVariantList categories() const;
    QStringList  navOrder() const { return m_navOrder; }
    QString      defaultPage() const { return m_defaultPage; }
    void         setDefaultPage(const QString &p);

signals:
    void snapshotChanged();
    void icloudStateChanged();
    void autoStartChanged();
    void settingsChanged();
    // void quotaChanged();   // 存储卡片暂缓
    void anomalyChanged();
    void logChanged();
    void driveRootChanged();
    void categoriesChanged();
    void navOrderChanged();
    void sidecarStateChanged();
    // 请求 shell 弹 Windows 系统通知(由 main.cpp 的托盘承接)。
    // 两个来源: ①刚进入卡死 ②设置页的"测试通知"按钮。
    void systemNotify(const QString &title, const QString &detail);
    // 给 QML 弹提示用
    void notify(const QString &kind, const QString &text);

private slots:
    // 旁路推送的一帧完整快照(字段名与上面的 Q_PROPERTY 同名)
    void onSidecarSnapshot(const QJsonObject &payload);
    // 旁路上下线。离线时立刻切到"监控不可用", 并在恢复时提示
    void onSidecarOnlineChanged(bool online, const QString &reason);
    void onIcloudFinished(bool ok, const QString &msg);
    void onIcloudProgress(const QString &msg);
    void onProcTimer();

private:
    // 把 payload 里的 anomalies 数组落到 m_anomaly* 上, 并复刻原有弹窗语义
    // (首次出现或换了种类才弹; 卡死走系统通知, 其余走窗口内 Toast)
    void applyAnomalies(const QJsonArray &arr);
    // 旁路离线: 清空数据 + 明确文案, 不沿用旧数据
    void applyOffline(const QString &reason);

    SidecarClient  *m_sidecar;
    IcloudService  *m_service;
    AutoStart      *m_autoStart;

    // 最近一帧快照(旁路 JSON 原样保存, getter 直接读它)
    QJsonObject m_payload;
    bool        m_sidecarOnline = false;
    QString     m_sidecarOfflineReason;
    // 最近一次从旁路收到的同步根目录。单独存、**不随 m_payload 清空** ——
    // 根目录是配置而非同步数据, 旁路离线时不该退回错误路径。
    QString     m_driveRoot;
    PendingModel      m_pendingModel;
    LocalPendingModel m_localModel;
    TrashModel        m_trashModel;

    // iCloud 存储配额: 暂缓(解析器未调通), 字段保留待启用
    // icloud::QuotaInfo m_quota;

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
    // 标题栏关闭按钮行为(持久化在 QSettings: ui/closeToTray)。
    // true  = 关闭窗口, 进程后台常驻(托盘图标保留);
    // false = 关闭窗口即退出程序。
    // 默认 false —— 与历史行为一致(点关闭即退出), 不改变老用户习惯。
    bool    m_closeToTray = false;
};

}  // namespace icloud

#endif  // BRIDGE_H
