#include "Bridge.h"

#include <QDateTime>
#include <QDir>
#include <QProcess>

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

// ── 快照处理 ──────────────────────────────────────────────────────
void Bridge::onSnapshot(const icloud::Snapshot &s)
{
    m_snap = s;
    m_pendingModel.setItems(s.pendingItems);
    m_localModel.setItems(s.localPendings);
    emit snapshotChanged();
}

void Bridge::onStateChanged(const icloud::Snapshot &s)
{
    m_snap = s;
    m_pendingModel.setItems(s.pendingItems);
    m_localModel.setItems(s.localPendings);
    emit snapshotChanged();
}

void Bridge::onAnomaly(const QString &kind, const QString &title,
                       const QString &detail)
{
    m_anomalous = true;
    m_anomalyTitle = title;
    m_anomalyDetail = detail;
    if (!m_anomalyList.contains(kind))
        m_anomalyList.append(kind);
    emit anomalyChanged();
    emit notify(kind == QStringLiteral("卡死") ? QStringLiteral("error")
                                               : QStringLiteral("warn"),
                QStringLiteral("[%1] %2").arg(kind, title));
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
