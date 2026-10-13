// ======================================================================
//  SystemNotifier.h  -  Windows 系统通知(通知中心)发送器
//
//  为什么需要它 / 为什么必须开线程:
//    QSystemTrayIcon::showMessage 在本机会**死锁**(实测精确卡在那一句,
//    日志里它之前的行有、之后的行没有, 进程只能强杀)。根因是它落到
//    WinRT Toast 的 COM 通道上, 而调用线程的单元(apartment)是 MTA,
//    Toast 需要 STA, 跨单元 marshal 直接把 UI 线程挂死。
//
//    所以把系统通知放到一个**专属 STA 工作线程**里做:
//      · 线程内 RoInitialize(RO_INIT_SINGLE_THREADED) —— 单元正确;
//      · 线程内跑消息泵(MsgWaitForMultipleObjectsEx + PeekMessage) ——
//        STA 必须泵消息, COM 才能把跨单元调用送进来;
//      · UI 线程只做"入队 + 唤醒", 毫秒级返回, 永不阻塞。
//
//  接口约定:
//    · notify() 纯投递, 不等待结果 —— 通知是"尽力送达"的东西,
//      不该让 iCloud 告警这条链路反过来卡住界面。
//    · 同一条告警反复触发不会堆一屏: 通知带固定 tag, 新的替换旧的。
//    · available() 表示系统通知通道当前是否可用(用户可在 Windows 设置里
//      把这个应用的通知关掉)。不可用时上层要退回窗口内提醒, 不能让告警
//      无声消失。
// ======================================================================
#ifndef ICLOUDMON_SYSTEMNOTIFIER_H
#define ICLOUDMON_SYSTEMNOTIFIER_H

#include <QObject>
#include <QString>

#include <atomic>
#include <deque>
#include <mutex>

namespace icloud {

class SystemNotifier : public QObject
{
    Q_OBJECT

public:
    explicit SystemNotifier(QObject *parent = nullptr);
    ~SystemNotifier() override;

    // 投递一条系统通知。非阻塞: 只把内容塞进队列并唤醒工作线程。
    void notify(const QString &title, const QString &body);

    // 系统通知通道是否可用(工作线程启动时探测, 之后随投递结果刷新)
    bool available() const { return m_available.load(); }

    // 停线程并等待退出。幂等, 可重复调用。
    // ★ 必须在 QApplication 析构前调用(main.cpp 的 aboutToQuit),
    //   否则线程可能活到进程收尾阶段 —— 那正是本程序历史上段错误的地方,
    //   不要往上再叠一层不确定因素。
    void shutdown();

signals:
    // 通道可用性变化(可能从工作线程发出, 接收方在主线程会自动排队投递)
    void availabilityChanged(bool available);

private:
    struct Job
    {
        QString title;
        QString body;
    };

    static unsigned __stdcall threadProc(void *self);
    void workerLoop();
    // 返回 false 表示这条没能投出去(会顺带刷新 available)
    bool drainOne(const Job &job);

    // ── Win32 句柄。故意用 void* 而不是 HANDLE, 让这个头文件不必拖进
    //    <windows.h>(那会往所有包含者身上带 min/max 宏等一堆污染)。
    void *m_thread = nullptr;
    void *m_wake = nullptr;    // 有新任务(自动重置事件)
    void *m_ready = nullptr;   // 工作线程消息队列已就绪

    std::mutex      m_mu;
    std::deque<Job> m_queue;
    std::atomic<bool> m_quit{false};
    std::atomic<bool> m_available{false};
    std::atomic<bool> m_stopped{false};
};

}  // namespace icloud

#endif  // ICLOUDMON_SYSTEMNOTIFIER_H
