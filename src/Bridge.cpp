#include "Bridge.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QDir>
#include <QProcess>
#include <QSettings>

#include "IcloudPaths.h"
#include "Log.h"

namespace icloud {

Bridge::Bridge(QObject *parent)
    : QObject(parent)
    , m_monitor(new IcloudMonitor(this))
    , m_service(new IcloudService(this))
    , m_autoStart(new AutoStart(this))
{
    connect(m_monitor, &IcloudMonitor::snapshotReady,
            this, &Bridge::onSnapshot);
    connect(m_monitor, &IcloudMonitor::stateChanged,
            this, &Bridge::onStateChanged);
    connect(m_monitor, &IcloudMonitor::anomalyDetected,
            this, &Bridge::onAnomaly);
    connect(m_monitor, &IcloudMonitor::anomalyCleared,
            this, &Bridge::onAnomalyCleared);

    connect(m_service, &IcloudService::finished,
            this, &Bridge::onIcloudFinished);
    connect(m_service, &IcloudService::progress,
            this, &Bridge::onIcloudProgress);

    // 侧边栏顺序与默认启动页(用户设置)
    loadNavSettings();

    // 进程状态轮询: 3s 一次, 比重启按钮的即时性好
    m_procTimer = new QTimer(this);
    m_procTimer->setInterval(3000);
    connect(m_procTimer, &QTimer::timeout, this, &Bridge::onProcTimer);
    m_procTimer->start();
    onProcTimer();

    startMonitoring();
}

Bridge::~Bridge() = default;

// ── 监控控制 ──────────────────────────────────────────────────────
void Bridge::startMonitoring()
{
    m_monitor->setStallSeconds(m_stallSec);
    m_monitor->start(m_refreshSec * 1000);
}

void Bridge::stopMonitoring()
{
    m_monitor->stop();
}

void Bridge::refreshNow()
{
    m_monitor->refresh();
}

// ── iCloud 控制 ───────────────────────────────────────────────────
void Bridge::restartIcloud()
{
    if (m_restarting)
        return;
    log::userAction(QStringLiteral("重启 iCloud"));
    m_restarting = true;
    emit icloudStateChanged();
    emit notify(QStringLiteral("info"), QStringLiteral("正在重启 iCloud..."));
    m_service->restart();
}

void Bridge::stopIcloud()
{
    if (m_restarting)
        return;
    log::userAction(QStringLiteral("停止 iCloud"));
    m_restarting = true;
    emit icloudStateChanged();
    m_service->stopAll();
}

void Bridge::startIcloud()
{
    if (m_restarting)
        return;
    log::userAction(QStringLiteral("启动 iCloud"));
    m_restarting = true;
    emit icloudStateChanged();
    m_service->startAll();
}

void Bridge::onIcloudFinished(bool ok, const QString &msg)
{
    log::userAction(QStringLiteral("重启 iCloud"), msg);
    m_restarting = false;
    emit icloudStateChanged();
    emit notify(ok ? QStringLiteral("ok") : QStringLiteral("error"), msg);
    onProcTimer();          // 立即刷新进程状态
    m_monitor->refresh();   // 重启后同步状态会变, 立刻采一次
}

void Bridge::onIcloudProgress(const QString &msg)
{
    emit notify(QStringLiteral("info"), msg);
}

void Bridge::onProcTimer()
{
    m_procList = m_service->runningProcesses();
    const bool now = !m_procList.isEmpty();
    if (now != m_icloudRunning) {
        m_icloudRunning = now;
        // 进程识别结果记一条: 既是运行记录, 也是"进程探测是否正常"的证据
        log::info(QStringLiteral("[RUN] iCloud 进程: %1")
                      .arg(m_procList.isEmpty()
                               ? QStringLiteral("无")
                               : m_procList.join(QLatin1Char(','))));
        emit icloudStateChanged();
    }
}

// ── 自启动 ────────────────────────────────────────────────────────
bool Bridge::autoStart() const
{
    return m_autoStart->isEnabled();
}

bool Bridge::setAutoStart(bool on)
{
    QString err;
    if (!m_autoStart->setEnabled(on, &err)) {
        log::userAction(QStringLiteral("设置开机自启动=%1").arg(int(on)),
                        QStringLiteral("失败: %1").arg(err));
        emit notify(QStringLiteral("error"),
                    QStringLiteral("设置自启动失败: %1").arg(err));
        return false;
    }
    log::userAction(QStringLiteral("设置开机自启动=%1").arg(int(on)),
                    QStringLiteral("成功"));
    emit autoStartChanged();
    emit notify(QStringLiteral("ok"),
                on ? QStringLiteral("已开启开机自启动")
                   : QStringLiteral("已关闭开机自启动"));
    return true;
}

// ── 历史 ──────────────────────────────────────────────────────────
void Bridge::resetHistory()
{
    m_monitor->resetHistory();
    log::userAction(QStringLiteral("清空卡死检测基线"), QStringLiteral("成功"));
    emit notify(QStringLiteral("ok"), QStringLiteral("已清空历史基线"));
}

QString Bridge::stateFilePath() const
{
    return m_monitor->stateFilePath();
}

void Bridge::openLogDir()
{
    const QString dir = log::logDir();
    if (dir.isEmpty() || !QDir(dir).exists()) {
        log::warn(QStringLiteral("日志目录不存在: %1").arg(dir));
        emit notify(QStringLiteral("error"), QStringLiteral("日志目录不存在"));
        return;
    }
    log::userAction(QStringLiteral("打开日志目录"), dir);
    // 用 explorer 而不是 QProcess::startDetached("cmd") ——
    // 后者在中文系统上容易因编码问题起不来
    QProcess::startDetached(QStringLiteral("explorer.exe"), { dir });
}

// ── 同步根目录 ────────────────────────────────────────────────────
QString Bridge::driveRoot() const
{
    return paths::driveRoot();
}

void Bridge::openDriveRoot()
{
    const QString dir = paths::driveRoot();
    if (!QDir(dir).exists()) {
        log::warn(QStringLiteral("同步根目录不存在: %1").arg(dir));
        emit notify(QStringLiteral("error"),
                    QStringLiteral("同步根目录不存在: %1").arg(dir));
        return;
    }
    log::userAction(QStringLiteral("打开 iCloud 目录"), dir);
    QProcess::startDetached(QStringLiteral("explorer.exe"), { dir });
}

bool Bridge::setDriveRoot(const QString &path)
{
    const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(path));

    // 先校验再存: 存一个不存在的路径进去, 之后每次打开都会失败,
    // 而用户很难想到是自己存错了路径。
    QDir probe(clean);
    if (!probe.exists()) {
        log::userAction(QStringLiteral("设置同步根目录=%1").arg(clean),
                        QStringLiteral("失败: 目录不存在"));
        emit notify(QStringLiteral("error"),
                    QStringLiteral("目录不存在: %1").arg(clean));
        return false;
    }
    // iCloud Drive 根目录里必然有 .icloud 隐藏文件, 拿它做二次确认,
    // 避免用户随手选了个普通文件夹却以为设置成功了。
    const bool looksLikeDrive = !probe.entryList(
                    QStringList() << QStringLiteral(".icloud"),
                    QDir::Hidden | QDir::Files).isEmpty();

    QString err;
    if (!paths::setDriveRoot(clean, &err)) {
        log::userAction(QStringLiteral("设置同步根目录=%1").arg(clean),
                        QStringLiteral("失败: %1").arg(err));
        emit notify(QStringLiteral("error"),
                    QStringLiteral("保存失败: %1").arg(err));
        return false;
    }

    log::userAction(QStringLiteral("设置同步根目录=%1").arg(clean),
                    looksLikeDrive ? QStringLiteral("成功(检测到 .icloud)")
                                   : QStringLiteral("成功(未检测到 .icloud)"));
    emit driveRootChanged();
    emit notify(QStringLiteral("ok"), QStringLiteral("同步根目录已更新"));
    // 路径变了, 立刻重采一次让界面跟上
    m_monitor->refresh();
    return true;
}

void Bridge::resetDriveRoot()
{
    paths::clearDriveRoot();
    log::userAction(QStringLiteral("恢复默认同步根目录"),
                    paths::driveRoot());
    emit driveRootChanged();
    emit notify(QStringLiteral("ok"), QStringLiteral("已恢复默认路径"));
    m_monitor->refresh();
}

// ── 快照处理 ──────────────────────────────────────────────────────
void Bridge::onSnapshot(const icloud::Snapshot &s)
{
    m_snap = s;
    m_pendingModel.setItems(s.pendingItems);
    m_localModel.setItems(s.localPendings);
    m_trashModel.setItems(s.trashItems);
    refreshCategories();
    emit snapshotChanged();
}

void Bridge::onStateChanged(const icloud::Snapshot &s)
{
    m_snap = s;
    m_pendingModel.setItems(s.pendingItems);
    m_localModel.setItems(s.localPendings);
    m_trashModel.setItems(s.trashItems);
    refreshCategories();
    emit snapshotChanged();
}

void Bridge::onAnomaly(const QString &kind, const QString &title,
                       const QString &detail)
{
    // 注意: 卡死这类异常会**每采样一次上报一次**(每 5 秒), 不是只在
    // 出现的那一次上报。所以这里的 flag 必须在赋值前先取, 判断"是不是新异常"。
    const bool wasAnomalous = m_anomalous;
    const bool isNewKind = !m_anomalyList.contains(kind);

    m_anomalous = true;
    m_anomalyTitle = title;
    m_anomalyDetail = detail;
    if (isNewKind)
        m_anomalyList.append(kind);
    emit anomalyChanged();      // 明细(停滞秒数等)要跟着刷新, 每次都发

    // 但弹窗只在新出现 / 换了异常种类时给。
    // 每次都弹的话, 持续卡死状态下 Toast 动画会被每 5 秒重启一次,
    // 既是持续打扰, 也让界面反复重绘。
    if (!wasAnomalous || isNewKind) {
        emit notify(kind == QStringLiteral("卡死") ? QStringLiteral("error")
                                                   : QStringLiteral("warn"),
                    QStringLiteral("[%1] %2").arg(kind, title));
        // 卡死是新出现时, 额外请求一次 Windows 系统通知。
        // 窗口最小化或在别的桌面时, 界面内的 Toast 是看不到的, 只有系统
        // 通知能真正提醒到人。
        if (kind == QStringLiteral("卡死")) {
            log::userAction(QStringLiteral("卡死告警触发系统通知"),
                            QStringLiteral("停滞 %1").arg(detail));
            emit systemNotify(title, detail);
        }
    }
}

void Bridge::testSystemNotification()
{
    log::userAction(QStringLiteral("测试系统通知"));
    emit systemNotify(
        QStringLiteral("iCloud 状态监控"),
        QStringLiteral("测试通知: 能弹出这条说明只要 iCloud 卡死, 你就能收到提醒。"));
}

// ── 四类同步 ──────────────────────────────────────────────────────
void Bridge::refreshCategories()
{
    // 相册/密码/书签要各复制并打开一个 SQLite 库, 比读云盘重。
    // 实测一次约 54ms, 跑在 GUI 线程上。这三类变化很慢, 没必要频繁查 ——
    // 每 6 个采样刷一次(默认约 30 秒), 把这点开销的占比压到 0.2% 以下。
    m_catTick++;
    if (!m_categories.isEmpty() && (m_catTick % 6) != 1)
        return;

    QElapsedTimer t;
    t.start();
    m_categories = IcloudCategories::readAll(m_snap);
    const qint64 ms = t.elapsed();

    // 只在"读得慢"时记耗时: 这段跑在 GUI 线程上, 慢了就是卡顿来源
    if (ms > 50)
        log::warn(QStringLiteral("[PERF] 四类状态读取偏慢 %1ms").arg(ms));

    // 四类状态变化时记一行 —— 这是看不到界面时验证数据链路的唯一手段
    QString sig;
    for (const CategoryStatus &c : m_categories) {
        sig += QStringLiteral("%1=%2(%3) ")
                   .arg(c.key,
                        c.enabled ? QStringLiteral("启用")
                                  : QStringLiteral("未启用"),
                        c.userSet ? QStringLiteral("手动")
                                  : QStringLiteral("自动"));
    }
    if (sig != m_lastCatSig) {
        m_lastCatSig = sig;
        log::info(QStringLiteral("[CAT] %1").arg(sig.trimmed()));
        for (const CategoryStatus &c : m_categories)
            log::info(QStringLiteral("[CAT] %1 摘要: %2")
                          .arg(c.name, c.summary));
    }

    emit categoriesChanged();
}

QVariantList Bridge::categories() const
{
    QVariantList out;
    out.reserve(m_categories.size());
    for (const CategoryStatus &c : m_categories) {
        QVariantList det;
        for (const CatDetail &d : c.details) {
            QVariantMap m;
            m.insert(QStringLiteral("label"), d.label);
            m.insert(QStringLiteral("value"), d.value);
            det.append(m);
        }
        QVariantMap m;
        m.insert(QStringLiteral("key"),      c.key);
        m.insert(QStringLiteral("name"),     c.name);
        m.insert(QStringLiteral("summary"),  c.summary);
        m.insert(QStringLiteral("enabled"),  c.enabled);
        m.insert(QStringLiteral("userSet"),  c.userSet);
        m.insert(QStringLiteral("autoOn"),   c.autoOn);
        m.insert(QStringLiteral("autoWhy"),  c.autoWhy);
        m.insert(QStringLiteral("hasData"),  c.hasData);
        m.insert(QStringLiteral("note"),     c.note);
        m.insert(QStringLiteral("details"),  det);
        out.append(m);
    }
    return out;
}

void Bridge::setCategoryEnabled(const QString &key, bool on)
{
    IcloudCategories::setUserValue(key, on);
    for (CategoryStatus &c : m_categories) {
        if (c.key == key) {
            c.userSet = true;
            c.enabled = on;
        }
    }
    emit categoriesChanged();
}

void Bridge::clearCategoryEnabled(const QString &key)
{
    IcloudCategories::clearUserValue(key);
    for (CategoryStatus &c : m_categories) {
        if (c.key == key) {
            c.userSet = false;
            c.enabled = c.autoOn;   // 回退到自动检测
        }
    }
    emit categoriesChanged();
}

// ── 侧边栏顺序 / 默认启动页 ───────────────────────────────────────
QString Bridge::categoryName(const QString &key) const
{
    for (const CategoryStatus &c : m_categories)
        if (c.key == key)
            return c.name;
    // 还没采到数据时的兜底, 保证顺序设置界面不会显示成 "drive"
    if (key == QStringLiteral("drive"))     return QStringLiteral("云盘");
    if (key == QStringLiteral("photos"))    return QStringLiteral("相册");
    if (key == QStringLiteral("passwords")) return QStringLiteral("密码");
    if (key == QStringLiteral("bookmarks")) return QStringLiteral("书签");
    return key;
}

void Bridge::loadNavSettings()
{
    QSettings s = paths::userSettings();

    const QStringList all = categoryKeys();
    QStringList order;
    for (const QString &k : s.value(QStringLiteral("nav/order")).toStringList())
        if (all.contains(k) && !order.contains(k))
            order << k;
    // 补齐缺失项(例如以后新增了类别), 否则该项会在侧边栏里消失
    for (const QString &k : all)
        if (!order.contains(k))
            order << k;
    m_navOrder = order;

    static const QStringList pages = {
        QStringLiteral("home"),      QStringLiteral("drive"),
        QStringLiteral("photos"),    QStringLiteral("passwords"),
        QStringLiteral("bookmarks"), QStringLiteral("settings")
    };
    const QString dp =
        s.value(QStringLiteral("nav/defaultPage")).toString();
    m_defaultPage = pages.contains(dp) ? dp : QStringLiteral("home");
}

void Bridge::saveNavOrder()
{
    QSettings s = paths::userSettings();
    s.setValue(QStringLiteral("nav/order"), m_navOrder);
    s.sync();
}

void Bridge::moveCategory(const QString &key, int delta)
{
    const int i = m_navOrder.indexOf(key);
    if (i < 0)
        return;
    const int j = i + delta;
    // 不允许越过边界 —— 首页和设置在导航里是固定的, 不在这张表里,
    // 所以这里的边界就是中间四类自己的首尾。
    if (j < 0 || j >= m_navOrder.size())
        return;

    m_navOrder.move(i, j);
    saveNavOrder();
    log::userAction(QStringLiteral("%1 移到第 %2 位")
                        .arg(categoryName(key)).arg(j + 1));
    emit navOrderChanged();
}

void Bridge::resetNavOrder()
{
    m_navOrder = categoryKeys();
    saveNavOrder();
    log::userAction(QStringLiteral("侧边栏顺序已恢复默认"));
    emit navOrderChanged();
}

void Bridge::setDefaultPage(const QString &p)
{
    static const QStringList pages = {
        QStringLiteral("home"),      QStringLiteral("drive"),
        QStringLiteral("photos"),    QStringLiteral("passwords"),
        QStringLiteral("bookmarks"), QStringLiteral("settings")
    };
    if (!pages.contains(p))
        return;
    m_defaultPage = p;
    QSettings s = paths::userSettings();
    s.setValue(QStringLiteral("nav/defaultPage"), p);
    s.sync();
    log::userAction(QStringLiteral("默认启动页 -> %1").arg(p));
    emit settingsChanged();
}

void Bridge::onAnomalyCleared()
{
    m_anomalous = false;
    m_anomalyTitle.clear();
    m_anomalyDetail.clear();
    m_anomalyList.clear();
    emit anomalyChanged();
    emit notify(QStringLiteral("ok"), QStringLiteral("同步已恢复正常"));
}

// ── getter 实现 ───────────────────────────────────────────────────
QString Bridge::stateName() const   { return m_snap.stateName(); }
QString Bridge::stateDetail() const { return m_snap.stateDetail(); }
bool    Bridge::busy() const        { return m_snap.isBusy(); }
bool    Bridge::inSync() const      { return m_snap.inSync; }
bool    Bridge::dbOk() const        { return m_snap.dbOk; }
QString Bridge::clientVersion() const { return m_snap.clientVersion; }
QString Bridge::timestamp() const
{
    return QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
}

int Bridge::upDone() const  { return m_snap.queue.syncedUpCount; }
int Bridge::upTotal() const
{
    return m_snap.queue.syncedUpCount + m_snap.queue.needUpCount;
}
QString Bridge::upSize() const
{
    return PendingModel::humanSize(m_snap.queue.needUpBytes);
}

int Bridge::dlDone() const  { return m_snap.queue.downloadedCount; }
int Bridge::dlTotal() const
{
    return m_snap.queue.downloadedCount + m_snap.queue.needDlCount;
}
QString Bridge::dlSize() const
{
    return PendingModel::humanSize(m_snap.queue.needDlBytes);
}
bool Bridge::dlBusy() const
{
    return m_snap.queue.needDlCount > 0 || m_snap.queue.pendingFetches > 0;
}

QString Bridge::lastError() const
{
    if (!m_snap.dbOk)
        return m_snap.dbError;
    if (m_snap.faults > 0)
        return QStringLiteral("故障项 %1 个").arg(m_snap.faults);
    if (!m_snap.uploadErrors.isEmpty()) {
        QStringList l;
        for (const auto &p : m_snap.uploadErrors)
            l << QStringLiteral("码%1 x%2").arg(p.first).arg(p.second);
        return QStringLiteral("上传错误 ") + l.join(QStringLiteral(", "));
    }
    if (!m_snap.downloadErrors.isEmpty()) {
        QStringList l;
        for (const auto &p : m_snap.downloadErrors)
            l << QStringLiteral("码%1 x%2").arg(p.first).arg(p.second);
        return QStringLiteral("下载错误 ") + l.join(QStringLiteral(", "));
    }
    return QString();
}

QString Bridge::rankText() const
{
    if (!m_snap.hasRank)
        return QStringLiteral("游标不可读");
    return QStringLiteral("client=%1  server=%2  待应用=%3  -> %4")
        .arg(m_snap.rankClient)
        .arg(m_snap.rankServer)
        .arg(m_snap.unapplied)
        .arg(m_snap.inSync ? QStringLiteral("已追平")
                           : QStringLiteral("追赶中"));
}

QVariantList Bridge::pcsBuckets() const
{
    QVariantList out;
    out.reserve(m_snap.pcsDist.size());
    for (const PcsBucket &b : m_snap.pcsDist) {
        QVariantMap m;
        m.insert(QStringLiteral("pcsState"), b.pcsState);
        m.insert(QStringLiteral("count"),    b.count);
        m.insert(QStringLiteral("bytes"),    double(b.bytes));
        m.insert(QStringLiteral("label"),    b.label);
        out.append(m);
    }
    return out;
}

void Bridge::setRefreshSec(int s)
{
    if (s < 1) s = 1;
    if (s > 3600) s = 3600;
    m_refreshSec = s;
    log::userAction(QStringLiteral("刷新间隔 -> %1 秒").arg(s));
    m_monitor->start(s * 1000);
    emit settingsChanged();
}

void Bridge::setStallSec(int s)
{
    if (s < 10) s = 10;
    if (s > 86400) s = 86400;
    m_stallSec = s;
    log::userAction(QStringLiteral("卡死阈值 -> %1 秒").arg(s));
    m_monitor->setStallSeconds(s);
    emit settingsChanged();
}

}  // namespace icloud
