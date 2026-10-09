#include "IcloudService.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

#include "IcloudPaths.h"

namespace icloud {

IcloudService::IcloudService(QObject *parent)
    : QObject(parent)
{
}

bool IcloudService::isRunning() const
{
    return !runningProcesses().isEmpty();
}

QStringList IcloudService::runningProcesses() const
{
    QStringList found;
    // tasklist /FI "IMAGENAME eq xxx.exe" /FO CSV /NH
    const auto probe = [](const QString &name) -> bool {
        QProcess p;
        p.start(QStringLiteral("tasklist"),
                { QStringLiteral("/FI"), QStringLiteral("IMAGENAME eq %1").arg(name),
                  QStringLiteral("/FO"), QStringLiteral("CSV"),
                  QStringLiteral("/NH") });
        if (!p.waitForFinished(4000))
            return false;
        return p.readAllStandardOutput().contains(name.toUtf8());
    };

    for (const QString &n : paths::icloudProcessNames()) {
        if (probe(n))
            found << n;
    }
    return found;
}

void IcloudService::restart()
{
    // 用成员标志避免 lambda 重连导致的重入问题:
    // stopAll 发 finished -> 若在 restart 流程中, 则延迟启动。
    m_restarting = true;
    emit progress(QStringLiteral("1/2 结束进程"));
    stopAll();
}

void IcloudService::emitFinished(bool ok, const QString &msg)
{
    if (m_restarting) {
        // stopAll 的结果不直接上报, 交给启动阶段统一报
        m_restarting = false;
        emit progress(QStringLiteral("2/2 启动进程"));
        // 等 1.5s 让文件锁与进程句柄彻底释放
        QTimer::singleShot(1500, this, [this]() { startAll(); });
        return;
    }
    emit finished(ok, msg);
}

void IcloudService::stopAll()
{
    const QStringList targets = paths::icloudProcessNames();
    if (targets.isEmpty()) {
        emitFinished(true, QStringLiteral("没有运行中的 iCloud 进程"));
        return;
    }

    // 一次性 taskkill 全部, 比逐个快
    QStringList args{ QStringLiteral("/F") };
    for (const QString &n : targets)
        args << QStringLiteral("/IM") << n;

    QProcess p;
    emit progress(QStringLiteral("正在结束 iCloud 进程..."));
    p.start(QStringLiteral("taskkill"), args);
    if (!p.waitForFinished(8000)) {
        p.kill();
        p.waitForFinished(2000);
    }
    // taskkill 返回 128 表示"进程不存在", 视为成功
    const int code = p.exitCode();
    if (code == 0 || code == 128) {
        emitFinished(true, QStringLiteral("已结束 %1 个 iCloud 进程")
                               .arg(targets.size()));
    } else {
        emitFinished(false, QStringLiteral("结束进程失败 (taskkill=%1)").arg(code));
    }
}

void IcloudService::startAll()
{
    // Store 应用不能用 CreateProcess 直接拉起, 必须走 explorer 的
    // shell:AppsFolder<AUMID> 形式。
    emit progress(QStringLiteral("正在启动 iCloud..."));

    const QString aumid = paths::icloudAumid();
    QProcess p;
    p.start(QStringLiteral("explorer.exe"),
            { QStringLiteral("shell:AppsFolder/%1").arg(aumid) });
    const bool started = p.waitForStarted(5000);

    if (!started) {
        // 回退: 尝试从开始菜单的 iCloud 快捷方式启动
        const QString appData =
            QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
        const QString lnk = appData + QStringLiteral("\\iCloud.lnk");
        if (QFile::exists(lnk)) {
            QProcess p2;
            p2.start(QStringLiteral("cmd"),
                     { QStringLiteral("/c"), QStringLiteral("start"), QString(),
                       QStringLiteral("\"%1\"").arg(lnk) });
            p2.waitForStarted(4000);
            emitFinished(true, QStringLiteral("已通过快捷方式启动 iCloud"));
        } else {
            emitFinished(false,
                QStringLiteral("启动失败: 未找到 AUMID %1, 也没有 iCloud.lnk")
                    .arg(aumid));
        }
        return;
    }

    // 启动后稍等, 让进程真正起来再报成功
    QTimer::singleShot(2500, this, [this]() {
        if (isRunning())
            emitFinished(true, QStringLiteral("iCloud 已启动"));
        else
            emitFinished(false, QStringLiteral("已发起启动, 但未检测到进程"));
    });
}

}  // namespace icloud
