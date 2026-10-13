#include "SidecarClient.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QStringList>
#include <QVariant>

#include "Log.h"

namespace icloud {

namespace {

// 看门狗: 超过 max(3*间隔, 20) 秒没收到任何事件就判离线。
// 用 max 而不是直接用 3*间隔 —— 用户把间隔调到 1 秒时 3 秒太敏感,
// 一次磁盘抖动就会闪离线再恢复, 界面会闪。
int watchdogMs(int refreshSec)
{
    return qMax(3 * refreshSec, 20) * 1000;
}

// 从起始目录逐级上溯寻找含 <sub> 的目录。开发期 exe 在 build/Release/ 下,
// 打包后 exe 与 sidecar/ 同级, 两种布局都能命中。
QString findUpwards(const QString &startDir, const QString &sub, int maxLevels = 6)
{
    QDir d(startDir);
    for (int i = 0; i <= maxLevels; ++i) {
        const QString cand = d.absoluteFilePath(sub);
        if (QFileInfo::exists(cand))
            return QDir::cleanPath(cand);
        if (!d.cdUp())
            break;
    }
    return QString();
}

}  // namespace

SidecarClient::SidecarClient(QObject *parent)
    : QObject(parent)
{
    m_python = findInterpreter();
    m_script = findScript();

    m_heartbeat = new QTimer(this);
    connect(m_heartbeat, &QTimer::timeout, this, &SidecarClient::onHeartbeat);

    m_restartTimer = new QTimer(this);
    m_restartTimer->setSingleShot(true);
    connect(m_restartTimer, &QTimer::timeout, this, [this]() {
        m_restartPending = false;
        if (!m_shuttingDown)
            launch();
    });
}

SidecarClient::~SidecarClient()
{
    shutdown();
}

// ── 解释器/脚本定位 ───────────────────────────────────────────────
QString SidecarClient::findInterpreter()
{
    // 1) 显式覆盖(调试用)
    const QByteArray env = qgetenv("ICLOUDMON_PYTHON");
    if (!env.isEmpty())
        return QString::fromLocal8Bit(env);

    // 2) 内嵌运行时(打包后): <exe目录>/sidecar/runtime/python.exe
    //    或开发期上溯到仓库根的 sidecar/runtime/python.exe
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList rel{
        QStringLiteral("sidecar/runtime/python.exe"),
        QStringLiteral("python-embed/python.exe"),
    };
    for (const QString &r : rel) {
        const QString hit = findUpwards(appDir, r);
        if (!hit.isEmpty())
            return hit;
    }

    // 3) PATH 兜底(开发机装了 Python 时最省事)
    return QStringLiteral("python");
}

QString SidecarClient::findScript()
{
    const QByteArray env = qgetenv("ICLOUDMON_SIDECAR");
    if (!env.isEmpty())
        return QString::fromLocal8Bit(env);

    const QString hit = findUpwards(QCoreApplication::applicationDirPath(),
                                    QStringLiteral("sidecar/serve.py"));
    if (!hit.isEmpty())
        return hit;
    // 都没找到也返回一个名义路径, 让错误信息里能看到期望位置
    return QDir::cleanPath(QCoreApplication::applicationDirPath()
                           + QStringLiteral("/sidecar/serve.py"));
}

// ── 进程生命周期 ──────────────────────────────────────────────────
bool SidecarClient::start()
{
    return launch();
}

bool SidecarClient::launch()
{
    if (m_shuttingDown)
        return false;
    if (m_proc && m_proc->state() != QProcess::NotRunning)
        return true;

    if (m_python.isEmpty() || m_script.isEmpty()) {
        goOffline(QStringLiteral("未找到旁路解释器或脚本"));
        return false;
    }

    delete m_proc;
    m_proc = new QProcess(this);
    m_proc->setProcessChannelMode(QProcess::MergedChannels);
    // 工作目录设为脚本所在目录的上一级(仓库根), 便于日志/相对路径行为一致
    m_proc->setWorkingDirectory(QFileInfo(m_script).absolutePath() + QStringLiteral("/.."));

    // ★ 强制 UTF-8: 中文 Windows 下 Python 默认 cp936, JSON 里一旦出现
    //   中文(如"已同步")就会乱码或解析失败。PYTHONUTF8/PYTHONIOENCODING
    //   两个都设, 覆盖 3.7+ 与更早版本的行为差异。
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PYTHONUTF8"), QStringLiteral("1"));
    env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    m_proc->setProcessEnvironment(env);

    connect(m_proc, &QProcess::readyReadStandardOutput,
            this, &SidecarClient::onReadyRead);
    connect(m_proc, QOverload<QProcess::ProcessError>::of(&QProcess::errorOccurred),
            this, &SidecarClient::onProcErrorOccurred);
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &SidecarClient::onProcFinished);

    m_buf.clear();
    m_readySeen = false;
    m_procDead = false;
    m_lastMsgMs = QDateTime::currentMSecsSinceEpoch();

    // -u: 关闭缓冲, 否则事件可能攒到进程退出才吐出来
    m_proc->start(m_python, { QStringLiteral("-u"), m_script });
    if (!m_proc->waitForStarted(5000)) {
        goOffline(QStringLiteral("旁路进程启动失败: %1").arg(m_python));
        scheduleRestart();
        return false;
    }

    m_heartbeat->start(1000);   // 1s 一跳: 既发 ping 也做看门狗
    log::info(QStringLiteral("[SIDECAR] 已启动 %1 %2")
                  .arg(m_python, m_script));
    return true;
}

void SidecarClient::shutdown()
{
    m_shuttingDown = true;
    m_heartbeat->stop();
    m_restartTimer->stop();
    if (!m_proc)
        return;
    if (m_proc->state() != QProcess::NotRunning) {
        // 先礼后兵: 写 quit 让它自己退, 再 terminate, 最后 kill
        m_buf.clear();
        send(QJsonObject{ { QStringLiteral("cmd"), QStringLiteral("quit") } });
        m_proc->closeWriteChannel();          // stdin EOF —— 旁路读循环会退出
        if (!m_proc->waitForFinished(2000)) {
            m_proc->terminate();
            if (!m_proc->waitForFinished(1500))
                m_proc->kill();
        }
        log::info(QStringLiteral("[SIDECAR] 已退出 (exit=%1)")
                      .arg(m_proc->exitCode()));
    }
}

// ── 收发 ──────────────────────────────────────────────────────────
void SidecarClient::send(const QJsonObject &cmd)
{
    if (!m_proc || m_proc->state() != QProcess::Running)
        return;
    QByteArray line = QJsonDocument(cmd).toJson(QJsonDocument::Compact);
    line.append('\n');
    m_proc->write(line);
}

void SidecarClient::onReadyRead()
{
    if (!m_proc || m_procDead)
        return;                          // 死进程的残留数据一律不认(见 m_procDead)
    m_buf.append(m_proc->readAllStandardOutput());
    // 只按换行切, 最后一段可能不完整, 留在缓冲里等下一次
    int idx;
    while ((idx = m_buf.indexOf('\n')) >= 0) {
        const QByteArray line = m_buf.left(idx);
        m_buf.remove(0, idx + 1);
        if (!line.trimmed().isEmpty())
            handleLine(line);
    }
}

void SidecarClient::handleLine(const QByteArray &line)
{
    if (m_procDead || m_shuttingDown)
        return;
    m_lastMsgMs = QDateTime::currentMSecsSinceEpoch();

    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(line, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        log::warn(QStringLiteral("[SIDECAR] 非法 JSON 行: %1")
                      .arg(QString::fromUtf8(line.left(160))));
        return;
    }
    const QJsonObject obj = doc.object();
    const QString evt = obj.value(QStringLiteral("evt")).toString();

    if (evt == QStringLiteral("ready")) {
        m_readySeen = true;
        m_restartDelayMs = 1000;              // 起来了就重置退避
        if (!m_online) {
            m_online = true;
            m_offlineReason.clear();
            emit onlineChanged(true, QString());
        }
        emit ready(obj.value(QStringLiteral("data")).toObject());
        // ready 之后补一次采样参数: 首启时 Bridge 的 setRefreshSec 可能比 ready
        // 更早发出(那时进程还在 import), 这里保证一定送达。用户已按过停止
        // 就别自作主张续采。
        if (m_wantSampling)
            setRefreshSec(m_refreshSec);
        return;
    }

    if (evt == QStringLiteral("snapshot")) {
        if (!m_online) {                       // 兜底: 没收到 ready 也认在线
            m_online = true;
            m_offlineReason.clear();
            emit onlineChanged(true, QString());
        }
        emit snapshotReady(obj.value(QStringLiteral("data")).toObject());
        return;
    }

    if (evt == QStringLiteral("error")) {
        m_lastError = obj.value(QStringLiteral("data")).toObject()
                         .value(QStringLiteral("message")).toString();
        log::warn(QStringLiteral("[SIDECAR] %1").arg(m_lastError));
        return;
    }

    // reply / bye 等: 目前只需保持看门狗活跃, 不必特殊处理
}

// ── 心跳与看门狗 ──────────────────────────────────────────────────
void SidecarClient::onHeartbeat()
{
    if (m_shuttingDown)
        return;

    if (!m_proc || m_proc->state() == QProcess::NotRunning) {
        goOffline(QStringLiteral("旁路进程未运行"));
        return;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastMsgMs > watchdogMs(m_refreshSec)) {
        goOffline(QStringLiteral("旁路无响应 %1 秒")
                      .arg((now - m_lastMsgMs) / 1000));
        // 无响应往往是进程卡死, 直接结束, 让 finished→scheduleRestart 重拉
        m_proc->kill();
        return;
    }

    // 未就绪时不停 ping, 让 ready 超时更快暴露
    if (!m_readySeen && now - m_lastMsgMs > 10000) {
        goOffline(QStringLiteral("旁路启动超时(10 秒内无 ready)"));
        m_proc->kill();
        return;
    }

    send(QJsonObject{ { QStringLiteral("cmd"), QStringLiteral("ping") } });
}

void SidecarClient::goOffline(const QString &reason)
{
    m_offlineReason = reason;
    if (m_online) {
        m_online = false;
        log::error(QStringLiteral("[SIDECAR] 离线: %1").arg(reason));
        emit onlineChanged(false, reason);
    }
}

void SidecarClient::scheduleRestart()
{
    if (m_shuttingDown || m_restartPending)
        return;
    m_restartPending = true;
    log::warn(QStringLiteral("[SIDECAR] %1 毫秒后重试拉起")
                  .arg(m_restartDelayMs));
    m_restartTimer->start(m_restartDelayMs);
    m_restartDelayMs = qMin(m_restartDelayMs * 2, 30000);
}

void SidecarClient::onProcErrorOccurred(QProcess::ProcessError e)
{
    m_procDead = true;                   // 先关门, 再判离线
    goOffline(QStringLiteral("旁路进程错误(code=%1)").arg(int(e)));
}

void SidecarClient::onProcFinished(int exitCode, QProcess::ExitStatus status)
{
    m_heartbeat->stop();
    m_procDead = true;
    if (m_shuttingDown)
        return;
    goOffline(QStringLiteral("旁路进程退出(exit=%1, %2)")
                  .arg(exitCode)
                  .arg(status == QProcess::CrashExit ? QStringLiteral("崩溃")
                                                     : QStringLiteral("正常")));
    scheduleRestart();
}

// ── 命令 ──────────────────────────────────────────────────────────
void SidecarClient::setRefreshSec(int seconds)
{
    m_refreshSec = qMax(1, qMin(3600, seconds));
    m_wantSampling = true;
    send(QJsonObject{
        { QStringLiteral("cmd"), QStringLiteral("start") },
        { QStringLiteral("interval"), m_refreshSec },
    });
}

void SidecarClient::stopSampling()
{
    m_wantSampling = false;
    send(QJsonObject{ { QStringLiteral("cmd"), QStringLiteral("stop") } });
}

void SidecarClient::refreshNow()
{
    send(QJsonObject{
        { QStringLiteral("cmd"), QStringLiteral("refresh") },
        { QStringLiteral("id"), 1 },
    });
}

void SidecarClient::resetHistory()
{
    send(QJsonObject{
        { QStringLiteral("cmd"), QStringLiteral("history.reset") },
        { QStringLiteral("id"), 1 },
    });
}

void SidecarClient::setSetting(const QString &key, const QVariant &value)
{
    send(QJsonObject{
        { QStringLiteral("cmd"), QStringLiteral("setting.set") },
        { QStringLiteral("key"), key },
        { QStringLiteral("value"), QJsonValue::fromVariant(value) },
    });
}

}  // namespace icloud
