// ======================================================================
//  SystemNotifier.cpp  -  Windows 系统通知投递(独立 STA 线程 + 消息泵)
//
//  实现要点(都已在独立探针里跑通, 见 doc/认证与取数路径评估.md 的说明):
//    1. 用 Windows SDK 的 WinRT ABI 头(winrt/*.h)直接调 Toast API。
//       刻意**不依赖 WRL / C++/WinRT** —— 本机 VS2019 未安装 wrl 头,
//       而 ABI 头 + 十几个 Ptr/HStr 小工具就够了, 少一层构建依赖。
//    2. 线程单元必须是 STA(SINGLE_THREADED), 且线程要**泵消息**。
//    3. UI 线程只入队 + SetEvent, 绝不等待 —— 通知失败也不能影响监控。
//    4. 通知带固定 tag: 同一条告警反复触发时**替换**而不是叠加。
// ======================================================================
#include "SystemNotifier.h"

#include "Log.h"

// ★ WIN32_LEAN_AND_MEAN / NOMINMAX / UNICODE 由 CMake 统一注入
//   (见 CMakeLists.txt 的 target_compile_definitions), 这里不再重复定义 ——
//   重复定义会刷 C4005 宏重定义警告。
#include <windows.h>
// ★ 常量名陷阱: SDK 里叫 **RO_INIT_SINGLETHREADED**(SINGLE 与 THREADED 之间
//   没有下划线)。写成 RO_INIT_SINGLE_THREADED 会报"未定义标识符", 很容易
//   误以为是 WINAPI_FAMILY 分区问题而跑去查头文件 —— 实际只是拼写。
//   roapi.h 里它被 WINAPI_FAMILY_PARTITION(...DESKTOP) 包着, 桌面工程下可见。
#include <winapifamily.h>
#include <unknwn.h>
#include <roapi.h>
#include <winstring.h>
#include <inspectable.h>
#include <windows.foundation.h>
#include <windows.data.xml.dom.h>
#include <windows.ui.notifications.h>

#include <process.h>

#include <string>

// 别名而不是 using namespace: 这些 ABI 类型名很短(IXmlNode 之类),
// 用 using 会把它们灌进整个 icloud 命名空间, 容易和以后的类型撞名。
namespace wun = ABI::Windows::UI::Notifications;
namespace wxd = ABI::Windows::Data::Xml::Dom;

namespace icloud {
namespace {

// 通知的应用标识(AUMID)。Windows 按它归集通知, 并在
// HKCU\SOFTWARE\Microsoft\Windows\CurrentVersion\Notifications\Settings\<AUMID>
// 下生成一条用户可管理的通知设置项。与注册表根/CompanyName 同名(iCloudMon)。
constexpr wchar_t kAumid[] = L"iCloudMon";
// 固定 tag: 反复告警只保留最新一条, 不在通知中心堆成一列。
constexpr wchar_t kTag[] = L"icloudmon";

UINT32 strLen(const wchar_t *s)
{
    UINT32 n = 0;
    while (s[n] != L'\0')
        ++n;
    return n;
}

// ── 极简 COM 智能指针 / HSTRING RAII ──
// 不引入 WRL: 只用到"取接口指针 + 用完 Release"和"造一个 HSTRING + 用完释放",
// 这两件事各自 15 行足够, 比多一层第三方依赖划算。
template <class T>
class Ptr
{
public:
    Ptr() = default;
    ~Ptr() { reset(); }
    Ptr(const Ptr &) = delete;
    Ptr &operator=(const Ptr &) = delete;

    void reset()
    {
        if (m_p) {
            m_p->Release();
            m_p = nullptr;
        }
    }
    T **put()
    {
        reset();
        return &m_p;
    }
    T *get() const { return m_p; }
    T *operator->() const { return m_p; }
    explicit operator bool() const { return m_p != nullptr; }

private:
    T *m_p = nullptr;
};

class HStr
{
public:
    explicit HStr(const wchar_t *s)
    {
        if (s)
            WindowsCreateString(s, strLen(s), &m_h);
    }
    ~HStr()
    {
        if (m_h)
            WindowsDeleteString(m_h);
    }
    HStr(const HStr &) = delete;
    HStr &operator=(const HStr &) = delete;

    operator HSTRING() const { return m_h; }

private:
    HSTRING m_h = nullptr;
};

// 取 WinRT 静态工厂(等价于激活这个类并 QI 到它的 statics 接口)
template <class T>
HRESULT activationFactory(const wchar_t *className, Ptr<T> &out)
{
    HStr cls(className);
    return RoGetActivationFactory(cls, __uuidof(T), (void **)out.put());
}

// 投递一条通知。返回 S_OK 表示已交给系统。
HRESULT showToast(const wchar_t *title, const wchar_t *body)
{
    Ptr<wun::IToastNotificationManagerStatics> mgr;
    HRESULT hr = activationFactory(
        L"Windows.UI.Notifications.ToastNotificationManager", mgr);
    if (FAILED(hr))
        return hr;

    // 用系统自带模板(ToastText02 = 标题 + 正文两行), 而不是手拼 XML:
    // 文本走 CreateTextNode, 标题正文里的 & < > 由系统自己转义, 不会拼坏。
    Ptr<wxd::IXmlDocument> xml;
    hr = mgr->GetTemplateContent(wun::ToastTemplateType_ToastText02, xml.put());
    if (FAILED(hr))
        return hr;

    Ptr<wxd::IXmlNodeList> nodes;
    hr = xml->GetElementsByTagName(HStr(L"text"), nodes.put());
    if (FAILED(hr))
        return hr;

    UINT32 count = 0;
    nodes->get_Length(&count);
    if (count < 2)
        return E_UNEXPECTED;

    for (UINT32 i = 0; i < 2; ++i) {
        Ptr<wxd::IXmlNode> node;
        if (FAILED(nodes->Item(i, node.put())))
            return E_FAIL;
        Ptr<wxd::IXmlText> textNode;
        if (FAILED(xml->CreateTextNode(HStr(i == 0 ? title : body),
                                      textNode.put())))
            return E_FAIL;
        // ★ ABI 头里 IXmlText 并没有声明继承 IXmlNode(与 WinRT 的类型
        //   层级不一致), 不能隐式转换, 必须 QI 到 IXmlNode 再 AppendChild。
        Ptr<wxd::IXmlNode> asNode;
        if (FAILED(textNode->QueryInterface(__uuidof(wxd::IXmlNode),
                                            (void **)asNode.put())))
            return E_FAIL;
        Ptr<wxd::IXmlNode> appended;
        if (FAILED(node->AppendChild(asNode.get(), appended.put())))
            return E_FAIL;
    }

    Ptr<wun::IToastNotificationFactory> factory;
    hr = activationFactory(L"Windows.UI.Notifications.ToastNotification",
                           factory);
    if (FAILED(hr))
        return hr;

    Ptr<wun::IToastNotification> toast;
    hr = factory->CreateToastNotification(xml.get(), toast.put());
    if (FAILED(hr))
        return hr;

    // Tag 挂在 IToastNotification2 上, 不在 IToastNotification 上
    Ptr<wun::IToastNotification2> toast2;
    if (SUCCEEDED(toast->QueryInterface(__uuidof(wun::IToastNotification2),
                                        (void **)toast2.put())))
        toast2->put_Tag(HStr(kTag));

    Ptr<wun::IToastNotifier> notifier;
    hr = mgr->CreateToastNotifierWithId(HStr(kAumid), notifier.put());
    if (FAILED(hr))
        return hr;

    return notifier->Show(toast.get());
}

// 探测通道可用性: 工厂建得出 + 系统允许本应用弹通知。
// 用户在 Windows 设置里关掉本应用的通知时 get_Setting 会给出非 Enabled,
// 这时必须让上层知道(否则告警会无声消失)。
HRESULT probeChannel(wun::NotificationSetting *settingOut)
{
    Ptr<wun::IToastNotificationManagerStatics> mgr;
    HRESULT hr = activationFactory(
        L"Windows.UI.Notifications.ToastNotificationManager", mgr);
    if (FAILED(hr))
        return hr;

    Ptr<wun::IToastNotifier> notifier;
    hr = mgr->CreateToastNotifierWithId(HStr(kAumid), notifier.put());
    if (FAILED(hr))
        return hr;

    wun::NotificationSetting s = wun::NotificationSetting_Enabled;
    hr = notifier->get_Setting(&s);
    if (settingOut)
        *settingOut = s;
    return hr;
}

const char *settingName(wun::NotificationSetting s)
{
    switch (s) {
    case wun::NotificationSetting_Enabled:
        return "Enabled";
    case wun::NotificationSetting_DisabledForApplication:
        return "DisabledForApplication";
    case wun::NotificationSetting_DisabledForUser:
        return "DisabledForUser";
    case wun::NotificationSetting_DisabledByGroupPolicy:
        return "DisabledByGroupPolicy";
    case wun::NotificationSetting_DisabledByManifest:
        return "DisabledByManifest";
    }
    return "Unknown";
}

}  // namespace

// ======================================================================
SystemNotifier::SystemNotifier(QObject *parent) : QObject(parent)
{
    // 自动重置事件: 有任务就置位, 工作线程等一次消费一次。
    // 自动重置保证"入队 + SetEvent"不会丢唤醒 —— 即使 SetEvent 发生在
    // 线程尚未开始等待时, 事件仍保持置位, 第一次等待立刻返回。
    m_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    m_ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!m_wake || !m_ready) {
        log::error(QStringLiteral("[NOTIFY] 事件对象创建失败, 系统通知不可用"));
        return;
    }

    unsigned tid = 0;
    m_thread = reinterpret_cast<void *>(_beginthreadex(
        nullptr, 0, &SystemNotifier::threadProc, this, 0, &tid));
    if (!m_thread) {
        log::error(QStringLiteral("[NOTIFY] 通知线程创建失败, 系统通知不可用"));
        return;
    }
    // 等消息队列就绪(正常是毫秒级)。这只是构造期的一次短等待,
    // 与"通知投递阻塞 UI"是两回事。
    WaitForSingleObject(static_cast<HANDLE>(m_ready), 3000);
}

SystemNotifier::~SystemNotifier()
{
    shutdown();
    if (m_wake)
        CloseHandle(static_cast<HANDLE>(m_wake));
    if (m_ready)
        CloseHandle(static_cast<HANDLE>(m_ready));
}

void SystemNotifier::notify(const QString &title, const QString &body)
{
    if (m_stopped.load())
        return;
    {
        std::lock_guard<std::mutex> lk(m_mu);
        // 上限保护: 通道不可用时没有消费者, 不能让队列无限长
        while (m_queue.size() >= 32)
            m_queue.pop_front();
        m_queue.push_back(Job{ title, body });
    }
    if (m_wake)
        SetEvent(static_cast<HANDLE>(m_wake));
}

void SystemNotifier::shutdown()
{
    bool expected = false;
    if (!m_stopped.compare_exchange_strong(expected, true))
        return;   // 已经在关了(幂等)
    {
        std::lock_guard<std::mutex> lk(m_mu);
        m_quit = true;
    }
    if (m_wake)
        SetEvent(static_cast<HANDLE>(m_wake));
    if (m_thread) {
        WaitForSingleObject(static_cast<HANDLE>(m_thread), 5000);
        CloseHandle(static_cast<HANDLE>(m_thread));
        m_thread = nullptr;
    }
}

unsigned __stdcall SystemNotifier::threadProc(void *self)
{
    static_cast<SystemNotifier *>(self)->workerLoop();
    return 0;
}

void SystemNotifier::workerLoop()
{
    // ★ 单元必须是 STA: Toast 的 COM 通道要求单线程单元, 用 MTA 会
    //   在 marshal 阶段挂死调用方(这正是当初 showMessage 死锁的原因)。
    const HRESULT hrInit = RoInitialize(RO_INIT_SINGLETHREADED);
    if (FAILED(hrInit)) {
        log::error(
            QStringLiteral("[NOTIFY] 通知线程 RoInitialize(STA) 失败: 0x%1")
                .arg(quint32(hrInit), 8, 16, QLatin1Char('0')));
        m_available = false;
        // ★ 失败路径也必须放行 m_ready: 构造函数在等它, 不放行就要白等 3 秒。
        if (m_ready)
            SetEvent(static_cast<HANDLE>(m_ready));
        emit availabilityChanged(false);
        return;
    }

    wun::NotificationSetting setting = wun::NotificationSetting_Enabled;
    const HRESULT hrProbe = probeChannel(&setting);
    const bool ok = SUCCEEDED(hrProbe)
                    && setting == wun::NotificationSetting_Enabled;
    m_available = ok;
    if (ok) {
        log::info(
            QStringLiteral("[NOTIFY] 系统通知通道可用 (AUMID=%1, STA tid=%2)")
                .arg(QString::fromWCharArray(kAumid))
                .arg(quint32(GetCurrentThreadId())));
    } else if (SUCCEEDED(hrProbe)) {
        log::warn(QStringLiteral("[NOTIFY] 系统通知被系统关闭 (%1) —— "
                                 "告警退回窗口内提醒; 可在 Windows 设置 > "
                                 "系统 > 通知 里为 iCloudMon 打开")
                      .arg(QString::fromLatin1(settingName(setting))));
    } else {
        log::warn(QStringLiteral("[NOTIFY] 系统通知通道不可用: 0x%1 —— "
                                 "告警退回窗口内提醒")
                      .arg(quint32(hrProbe), 8, 16, QLatin1Char('0')));
    }
    emit availabilityChanged(ok);

    // ★ 先把消息队列建出来, 再对外宣告就绪 —— COM 在这个线程上要用它。
    MSG msg;
    PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    SetEvent(static_cast<HANDLE>(m_ready));

    for (;;) {
        // 同时等"有新任务"和"窗口消息": STA 线程必须泵消息, COM 才能把
        // 跨单元调用投递进来。纯 GetMessage 也能泵, 但那样没法被事件唤醒
        // 可靠处理队列(只能再绕一次 PostThreadMessage), 这里一并解决。
        HANDLE waitHandles[1] = { static_cast<HANDLE>(m_wake) };
        const DWORD r = MsgWaitForMultipleObjectsEx(1, waitHandles, INFINITE,
                                                    QS_ALLINPUT,
                                                    MWMO_INPUTAVAILABLE);

        if (r == WAIT_OBJECT_0) {
            ResetEvent(static_cast<HANDLE>(m_wake));
            for (;;) {
                Job job;
                {
                    std::lock_guard<std::mutex> lk(m_mu);
                    if (m_queue.empty()) {
                        if (m_quit)
                            goto done;
                        break;
                    }
                    job = m_queue.front();
                    m_queue.pop_front();
                }
                if (!drainOne(job) && m_available.exchange(false))
                    emit availabilityChanged(false);
            }
        } else if (r == WAIT_OBJECT_0 + 1) {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        } else {
            break;   // 等待失败(句柄失效等): 退出线程, 不要再空转
        }
    }

done:
    RoUninitialize();
}

bool SystemNotifier::drainOne(const Job &job)
{
    const std::wstring title = job.title.toStdWString();
    const std::wstring body = job.body.toStdWString();
    const HRESULT hr = showToast(title.c_str(), body.c_str());
    if (SUCCEEDED(hr)) {
        log::info(QStringLiteral("[NOTIFY] 已发系统通知: %1").arg(job.title));
        return true;
    }
    log::warn(QStringLiteral("[NOTIFY] 系统通知发送失败 0x%1: %2")
                  .arg(quint32(hr), 8, 16, QLatin1Char('0'))
                  .arg(job.title));
    return false;
}

}  // namespace icloud
