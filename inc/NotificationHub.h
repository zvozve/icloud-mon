#pragma once
#include <QObject>
#include <QString>

namespace icloud {

// ── 浮层通知中枢 ───────────────────────────────────────────────
// C++ 侧(Bridge 的 notify / systemNotify)把通知投递到这里,
// 再由 QML 侧的 Toast.qml 监听 requested 信号显示出来。
//
// 为什么要绕这一层, 而不是直接用 QSystemTrayIcon::showMessage:
// 后者在本机走 Windows 的 WinRT Toast COM, 而 Qt 主线程是 MTA、
// Toast 需要 STA, marshaling 直接**死锁** —— 实测精确卡死在
// showMessage 一句(日志里"已投递"之前的行有、之后的行没有, 进程被强杀)。
// 自建浮层是纯 Qt Quick, 跑在 UI 线程, 绝不阻塞; 且是独立置顶窗口,
// 主窗口最小化时也能弹出来(满足"iCloud 卡死时人在别的程序"的提醒需求)。
class NotificationHub : public QObject {
    Q_OBJECT
public:
    explicit NotificationHub(QObject *parent = nullptr);

    // 给 C++ 调用: 投递一条通知。kind 决定配色/样式
    //   ("error" 红 / "warn" 黄 / "ok" 绿 / "system" 蓝 / 其他 灰)。
    Q_INVOKABLE void post(const QString &kind,
                          const QString &title,
                          const QString &body);

signals:
    // QML 浮层(Toast.qml)监听这个信号来显示。
    void requested(const QString &kind,
                   const QString &title,
                   const QString &body);
};

} // namespace icloud
