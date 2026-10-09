// windows.h 要放在 Qt 头文件之前, 避免顺序引起的宏冲突。
// 项目全局已定义 WIN32_LEAN_AND_MEAN 与 NOMINMAX。
#include <windows.h>
#include <tlhelp32.h>

#include "IcloudService.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QHash>
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
    // 用 Toolhelp32 快照一次性枚举全部进程, 不派生任何子进程。
    //
    // ★ 别改回逐个 tasklist。原来的写法是对 4 个进程名各起一次
    //   `tasklist /FI ...` 并 waitForFinished(4000) **阻塞等待** ——
    //   每 3 秒就在 GUI 线程上产生 4 次进程创建 + 4 次阻塞等待,
    //   每次进程创建加枚举约 50~100ms, 合计 200~400ms 的主线程停顿。
    //   这就是拖动窗口明显卡顿的主要来源(且每 3 秒必发生一次)。
    //   现在只读一次内核快照, 耗时 1ms 量级。
    QStringList found;

    QHash<QString, QString> want;   // 小写名 -> 原始名
    for (const QString &n : paths::icloudProcessNames())
        want.insert(n.toLower(), n);

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return found;

    PROCESSENTRY32W pe;
    ZeroMemory(&pe, sizeof(pe));
    pe.dwSize = sizeof(pe);

    if (Process32FirstW(snap, &pe)) {
        do {
            const QString name =
                QString::fromWCharArray(pe.szExeFile).toLower();
            const auto it = want.constFind(name);
            if (it != want.constEnd()) {
                // 同一进程名可能有多个实例, 按名字去重
                if (!found.contains(it.value()))
                    found << it.value();
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
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
    // shell:AppsFolder\<AUMID> 形式。
    const QString aumid = paths::icloudAumid();
    emit progress(QStringLiteral("正在启动 iCloud (%1)...").arg(aumid));

    QProcess p;
    p.start(QStringLiteral("explorer.exe"),
            { QStringLiteral("shell:AppsFolder/%1").arg(aumid) });
    const bool started = p.waitForStarted(5000);
    // explorer 交接完就退出, 这里等它落地, 否则 QProcess 析构会警告
    p.waitForFinished(3000);

    if (!started) {
        // 回退: 从开始菜单的 iCloud 快捷方式启动。
        // 用 explorer 打开 .lnk 而不是 `cmd /c start` —— 后者依赖 cmd,
        // 且在中文系统上容易因编码问题起不来。
        const QString appData =
            QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
        const QString lnk = appData + QStringLiteral("\\iCloud.lnk");
        if (QFile::exists(lnk)) {
            QProcess p2;
            p2.start(QStringLiteral("explorer.exe"), { lnk });
            p2.waitForStarted(4000);
            p2.waitForFinished(3000);
            emitFinished(true, QStringLiteral("已通过快捷方式启动 iCloud"));
        } else {
            emitFinished(false,
                QStringLiteral("启动失败: AUMID %1 未生效, 也没有 iCloud.lnk")
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
