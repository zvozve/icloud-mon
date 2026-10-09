// ======================================================================
//  IcloudMonitor.h  -  读 iCloudDrive.db 拿真实同步状态 (核心数据层)
//
//  关键实现要点 (踩过的坑, 别改):
//  1. 必须把 db + -wal + -shm 三个文件复制到临时目录再只读打开。
//     直连原库会撞 iCloud 的写锁; 且 WAL 里未 checkpoint 的数据读不到。
//     只复制 .db 会丢掉全部待同步队列, 表现为"永远显示空闲"。
//  2. 复制在后台线程做(约 16MB), 绝不能卡 UI 线程。
//  3. item_recursive_properties 的计数器在空闲时全为 0, 不是历史累计值。
//     判断"在忙"必须联合 local_items / server_items / server_zones。
// ======================================================================
#ifndef ICLOUDMONITOR_H
#define ICLOUDMONITOR_H

#include <QJsonObject>
#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QVector>

#include "IcloudSnapshot.h"

QT_FORWARD_DECLARE_CLASS(QTimer)

namespace icloud {

// ── 卡死检测结果 ──────────────────────────────────────────────────
struct StallInfo
{
    bool    stalled   = false;  // 是否判定卡死
    int     seconds   = 0;      // 停滞秒数
    int     observed  = 0;      // 已观测总时长
    QString detail;             // 卡住的活动内容
};

class IcloudMonitor : public QObject
{
    Q_OBJECT

public:
    explicit IcloudMonitor(QObject *parent = nullptr);
    ~IcloudMonitor() override;

    // 启动/停止定时采样
    void start(int intervalMs = 5000);
    void stop();

    // 卡死判定阈值(秒)。默认 300 = 5 分钟。
    void setStallSeconds(int s) { m_stallSeconds = s; }
    int  stallSeconds() const   { return m_stallSeconds; }

    // 立即触发一次采样 (异步, 结果走 snapshotReady 信号)
    void refresh();

    // 历史基线文件路径
    QString stateFilePath() const;

    // 清空历史基线
    Q_INVOKABLE void resetHistory();

    // 供 UI 同步查询 (最近一次结果)
    Snapshot lastSnapshot() const { return m_last; }

signals:
    // 每次采样完成 (无论是否有变化)
    void snapshotReady(const icloud::Snapshot &snapshot);
    // 状态发生变化时发出 (UI 可只在这个信号里刷新重绘)
    void stateChanged(const icloud::Snapshot &snapshot);
    // 卡死或异常
    void anomalyDetected(const QString &kind,
                         const QString &title,
                         const QString &detail);
    // 恢复正常
    void anomalyCleared();

private slots:
    void onTimeout();

private:
    // 采样主流程:复制数据库快照 + 查库 + 判定, 全程在主线程。
    void doSample();
    // 打开复制后的数据库并读取全部字段
    bool readSnapshot(Snapshot &out, const QString &dbPath, QString &err);
    // 复制 db + wal + shm 到临时目录, 返回临时目录路径
    bool stageDatabase(QString &dbPath, QString &err);
    // 建立/释放只读连接。
    // 关键: 连接必须是【局部变量】, 不能存成成员。
    // 存成成员后再拷一份局部副本去 exec, 会因两个句柄拉扯导致
    // exec 报 "Driver not loaded" —— 建/用/销必须严格同一作用域。
    // 另: Qt 的 QSQLITE 默认不解析 file: URI, 必须开 QSQLITE_OPEN_URI,
    // 否则会把整个 "file:...?mode=ro" 当文件名, 必然打不开。
    static bool openConnection(const QString &dbPath, QString &err,
                               QSqlDatabase *out);
    static void closeConnection(QSqlDatabase *db);

    // 活动指纹: 只含随同步推进而变化的字段, 绝不含时间戳
    static QJsonObject fingerprint(const Snapshot &s);
    // 追加历史样本并返回历史总条数
    int  appendHistory(const Snapshot &s);
    // 从历史判定卡死
    StallInfo detectStall();

    QTimer *m_timer = nullptr;
    Snapshot m_last;
    QString  m_stateFile;
    int      m_stallSeconds = 300;   // 卡死阈值(秒)

    // 上一帧指纹, 用于判断 stateChanged
    QJsonObject m_lastFp;
    // 上一帧是否处于异常 (用于 anomalyCleared)
    bool m_lastAnomalous = false;
    // 上一帧的状态名 / 数据库可读性 —— 用于日志里只记"变化"而非每帧刷屏
    QString m_lastStateName;
    bool    m_lastDbOk = false;
};

}  // namespace icloud

#endif  // ICLOUDMONITOR_H
