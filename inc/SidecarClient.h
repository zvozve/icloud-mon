// ======================================================================
//  SidecarClient.h  -  Python 旁路进程的客户端 (方案 A 的 Qt 侧)
//
//  职责(只这三件, 别加):
//    1) 拉起/看护/终止 sidecar/serve.py 进程
//    2) 按 NDJSON 协议收发命令与事件
//    3) 把 snapshot 事件原样交给 Bridge
//
//  ★ 不做的事: 不读 iCloudDrive.db、不做状态判定、不启停 iCloud。
//    取数与判定在 Python 侧, 启停归 IcloudService(C++)。
//
//  离线判定: 心跳 ping + 看门狗。**离线时 Bridge 必须停止沿用旧数据**,
//  否则旁路挂掉后界面会继续显示"已同步", 与本工具的目的直接冲突。
// ======================================================================
#ifndef SIDECARCLIENT_H
#define SIDECARCLIENT_H

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>
#include <QVariant>

namespace icloud {

class SidecarClient : public QObject
{
    Q_OBJECT

public:
    explicit SidecarClient(QObject *parent = nullptr);
    ~SidecarClient() override;

    // 拉起旁路进程。返回 false 表示进程没能启动(此时已进入离线态)。
    bool start();
    // 主动停止并等待退出(退出主程序时调用, 避免残留进程)
    void shutdown();

    // ── 命令 ──
    // 设置采样间隔并开始周期采样(秒)
    void setRefreshSec(int seconds);
    void stopSampling();
    void refreshNow();
    void resetHistory();
    void setSetting(const QString &key, const QVariant &value);

    bool    online() const { return m_online; }
    QString offlineReason() const { return m_offlineReason; }
    QString lastError() const { return m_lastError; }
    // 供界面/日志展示: 实际使用的解释器与脚本路径
    QString interpreterPath() const { return m_python; }
    QString scriptPath() const { return m_script; }

signals:
    // 一帧完整快照(字段名与 Bridge 的 Q_PROPERTY 同名)
    void snapshotReady(const QJsonObject &payload);
    // 上下线切换。reason 在离线时给出原因
    void onlineChanged(bool online, const QString &reason);
    void ready(const QJsonObject &info);

private slots:
    void onReadyRead();
    void onProcErrorOccurred(QProcess::ProcessError e);
    void onProcFinished(int exitCode, QProcess::ExitStatus status);
    void onHeartbeat();

private:
    void handleLine(const QByteArray &line);
    void send(const QJsonObject &cmd);
    void goOffline(const QString &reason);
    void scheduleRestart();
    bool launch();

    static QString findInterpreter();
    static QString findScript();

    QProcess *m_proc = nullptr;
    QTimer   *m_heartbeat = nullptr;   // 发 ping + 看门狗
    QTimer   *m_restartTimer = nullptr;

    QByteArray m_buf;
    QString    m_python;
    QString    m_script;

    bool    m_online = false;
    bool    m_readySeen = false;
    bool    m_shuttingDown = false;
    // 本轮子进程已判定死亡。★ 闸门作用:
    //   Windows 上进程死掉后 stdout 里可能还剩一帧没读完, readyRead 会晚于
    //   errorOccurred/finished 到达。若不拦住, handleLine 里 snapshot 分支那句
    //   "兜底认在线"会把状态抬回 true, 于是: 刚切完"监控不可用"又被一帧死后
    //   数据覆盖回正常值 —— 界面在离线瞬间闪一下旧状态, 随后才真正离线。
    bool    m_procDead = false;
    // 用户是否要求采样。旁路崩溃重拉后据此决定要不要续采 ——
    // 不能让"用户已按停止"被一次自动重启悄悄推翻。
    bool    m_wantSampling = false;
    QString m_offlineReason;
    QString m_lastError;

    qint64  m_lastMsgMs = 0;      // 最近一次收到任何事件的时刻
    int     m_refreshSec = 5;
    int     m_restartDelayMs = 1000;   // 退避: 1s → 2 → 4 → 8 → 15 → 30(封顶)
    bool    m_restartPending = false;
};

}  // namespace icloud

#endif  // SIDECARCLIENT_H
