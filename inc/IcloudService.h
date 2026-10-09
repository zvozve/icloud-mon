// ======================================================================
//  IcloudService.h  -  iCloud 进程控制 (重启/状态检测)
// ======================================================================
#ifndef ICLOUDSERVICE_H
#define ICLOUDSERVICE_H

#include <QObject>
#include <QStringList>

namespace icloud {

class IcloudService : public QObject
{
    Q_OBJECT

public:
    explicit IcloudService(QObject *parent = nullptr);

    // iCloud 是否在运行 (iCloudDrive.exe 存在)
    Q_INVOKABLE bool isRunning() const;

    // 当前运行的 iCloud 进程列表
    Q_INVOKABLE QStringList runningProcesses() const;

    // 重启: 结束全部 iCloud 进程后重新拉起
    // 异步执行, 结果通过 finished(bool ok, QString msg) 返回
    Q_INVOKABLE void restart();

    // 单独停止 / 启动
    Q_INVOKABLE void stopAll();
    Q_INVOKABLE void startAll();

signals:
    void finished(bool ok, const QString &message);
    // 进度提示, 让 UI 能显示"正在结束 xxx.exe"
    void progress(const QString &message);

private:
    void emitFinished(bool ok, const QString &msg);

    // restart() 流程标志: 为 true 时 stopAll 的 finished 不上报,
    // 而是转去"延迟启动"阶段。避免用信号重连造成的重入。
    bool m_restarting = false;
};

}  // namespace icloud

#endif  // ICLOUDSERVICE_H
