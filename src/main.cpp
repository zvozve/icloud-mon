// ======================================================================
//  main.cpp  -  iCloud Drive 同步状态监控器
//
//  数据来源: iCloudDrive.db (SQLite), 只读。
//  界面: QML (Qt Quick Controls 2), 深色主题。
//  日志: spdlog, 按天切分, 输出到 exe 同级\log\
// ======================================================================
// windows.h 要放在 Qt 头文件之前, 避免顺序引起的宏冲突。
// 项目全局已定义 WIN32_LEAN_AND_MEAN 与 NOMINMAX。
// 这里需要它是为了 RegDeleteTree/RegDeleteKey —— QSettings::clear() 只清
// <org>\<app> 下的键值, 删不掉空的 org 父键(见 migrateLegacyUserData)。
#include <windows.h>

#include <utility>
#include <memory>

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFont>
#include <QGuiApplication>
#include <QHash>
#include <QIcon>
#include <QImage>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMenu>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QPainter>
#include <QPixmap>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QSystemTrayIcon>
#include <QTimer>

#include "Bridge.h"
#include "SystemNotifier.h"
#include "IcloudPaths.h"
#include "Log.h"

namespace {

// 把 Qt 的告警/错误转发进 spdlog, 让 QML 报错也进日志文件。
// QML 的加载失败如果只往 stderr 打, 关掉控制台就彻底看不到根因。
//
// ★ 必须限流: Qt 有些警告会以极高频率重复。实测本机在剪贴板被其他进程
// 占用时, Qt 的 QWindowsClipboard 会以约 50ms 的间隔反复重试并告警 ——
// 而 Qt 的重试实现里带阻塞 Sleep, 直接在 GUI 线程上停顿, 这就是拖动窗口
// 卡的直接来源。把这些告警逐条写盘只会雪上加霜。
//
// 限流必须按"消息内容 + 时间窗口"做, 不能只比对上一条:
// 剪贴板那组是 "Retrying..." / "Unable to..." **交替**出现的,
// 相邻两条永远不同, 只比对上一句完全抑制不住。
void routeQtMessagesToLog()
{
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &,
                              const QString &msg) {
        // 必须 toUtf8(): spdlog 按 UTF-8 处理, 而 Qt 消息是 QString,
        // 直接 toStdString() 会按本地代码页(GBK) 转换, 中文全变乱码。
        const std::string m = msg.toUtf8().toStdString();

        static QHash<QString, qint64> lastSeen;
        static QHash<QString, int>    repeats;
        // 消息种类不会多, 超过这个量说明有异常, 直接清空防止无限增长
        if (lastSeen.size() > 256) {
            lastSeen.clear();
            repeats.clear();
        }
        constexpr qint64 kWindowMs = 10000;

        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const auto it = lastSeen.constFind(msg);
        if (it != lastSeen.constEnd() && (now - it.value()) < kWindowMs) {
            ++repeats[msg];
            return;
        }
        if (repeats.value(msg) > 0) {
            spdlog::warn("[QT] 上一条消息 10s 内重复 {} 次, 已抑制: {}",
                         repeats.value(msg), m);
            repeats[msg] = 0;
        }
        lastSeen[msg] = now;

        switch (type) {
        case QtDebugMsg:
        case QtInfoMsg:
            spdlog::debug("[QT] {}", m);
            break;
        case QtWarningMsg:
            spdlog::warn("[QT] {}", m);
            break;
        case QtCriticalMsg:
            spdlog::error("[QT] {}", m);
            break;
        case QtFatalMsg:
            spdlog::critical("[QT] {}", m);
            break;
        }
    });
}

// 补齐插件搜索路径。
// libraryPaths 默认只有 exe 目录, 不含 sqldrivers / platforms / styles
// 这些子目录; windeployqt 正是按这个布局部署的。不补的话SQL 驱动
// 加载会静默失败(open() 返回 true 但 exec() 报 "Driver not loaded")。
void addPluginPaths()
{
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QStringList subs = { QStringLiteral("sqldrivers"),
                               QStringLiteral("platforms"),
                               QStringLiteral("styles") };
    for (const QString &sub : subs) {
        const QString p = exeDir + QDir::separator() + sub;
        if (QDir(p).exists() && !QCoreApplication::libraryPaths().contains(p))
            QCoreApplication::addLibraryPath(p);
    }
}

// 预热 SQLite 驱动并体检。
// 驱动必须在主线程加载(Qt 不允许非主线程加载插件), 且越早失败越容易定位。
bool preloadSqliteDriver(QString *err)
{
    QSqlDatabase boot = QSqlDatabase::addDatabase(
        QStringLiteral("QSQLITE"), QStringLiteral("icm_boot"));
    boot.setDatabaseName(QStringLiteral(":memory:"));
    if (!boot.open()) {
        if (err)
            *err = boot.lastError().text();
        boot = QSqlDatabase();       // 先松手再注销
        QSqlDatabase::removeDatabase(QStringLiteral("icm_boot"));
        return false;
    }
    QSqlQuery probe(boot);
    if (!probe.exec(QStringLiteral("SELECT 1"))) {
        if (err)
            *err = probe.lastError().text();
        boot.close();
        boot = QSqlDatabase();
        QSqlDatabase::removeDatabase(QStringLiteral("icm_boot"));
        return false;
    }
    boot.close();
    boot = QSqlDatabase();
    QSqlDatabase::removeDatabase(QStringLiteral("icm_boot"));
    return true;
}

// 记录实际使用的图形后端。
//
// 拖动窗口卡顿的头号原因就是退化到**软件渲染**(opengl32sw / llvmpipe /
// SwiftShader) —— 那是 CPU 逐像素画, 1080p 下拖动必然卡。
// 这一点从进程外部看不出来(该 DLL 装载了不代表在用), 必须把 GL 的
// renderer 字符串打出来才算有证据:
//   renderer 含 "llvmpipe" / "SwiftShader" / "GDI Generic"  => 软件渲染
//   renderer 是 Intel/NVIDIA/AMD/Radeon 或 "ANGLE (..., Direct3D11)" => 硬件
void logGraphicsBackend(QQmlApplicationEngine &engine)
{
    if (engine.rootObjects().isEmpty())
        return;
    auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    if (!win)
        return;

    // ★ 先立刻记下"解析到的图形后端"。这一条**不依赖场景图是否成功**,
    //   白屏/场景图初始化失败时 sceneGraphInitialized 根本不触发, 旧探针
    //   在最需要的时候是瞎的。经 rendererInterface() 取(拿不到就记 unknown)。
    //
    // ★ 枚举名在 Qt5 / Qt6 之间变过, 这里按 **Qt6** 写:
    //     Qt5.15: OpenGL / Direct3D12 / OpenVG / OpenGLRhi / Direct3D11Rhi / VulkanRhi / MetalRhi / NullRhi
    //     Qt6.8 : OpenVG / OpenGL / Direct3D11 / Vulkan / Metal / Null / Direct3D12
    //             (且 OpenGLRhi=OpenGL、Direct3D11Rhi=Direct3D11 … 全部是**别名**)
    //   所以不能照 Qt5 那样把 OpenGL 和 OpenGLRhi 各列一个 case —— 在 Qt6 里
    //   它们是同一个值, MSVC 直接报 C2196 "case 值已使用"。只列 Qt6 的规范名。
    auto apiName = [](QSGRendererInterface::GraphicsApi api) -> const char * {
        switch (api) {
        case QSGRendererInterface::Software:   return "Software(软件渲染)";
        case QSGRendererInterface::OpenVG:     return "OpenVG";
        case QSGRendererInterface::OpenGL:     return "OpenGL";
        case QSGRendererInterface::Direct3D11: return "Direct3D11";
        case QSGRendererInterface::Direct3D12: return "Direct3D12";
        case QSGRendererInterface::Vulkan:     return "Vulkan";
        case QSGRendererInterface::Metal:      return "Metal";
        case QSGRendererInterface::Null:       return "Null(空)";
        default:                               return "Unknown";
        }
    };
    if (QSGRendererInterface *ri = win->rendererInterface())
        // fromUtf8 而不是 fromLatin1: apiName() 里那几个分支(name)是源码里的
        // **UTF-8 中文字面量**, 按 Latin-1 解码会把"软件渲染"打成 è½¯ä»¶æ¸²æ。
        icloud::log::info(QStringLiteral("[GPU] 解析到的图形后端: %1")
                              .arg(QString::fromUtf8(apiName(ri->graphicsApi()))));
    else
        icloud::log::info(QStringLiteral("[GPU] 解析到的图形后端: rendererInterface 未就绪"));

    // ★ 判"场景图有没有起来"用 isSceneGraphInitialized() 直接查, **不靠信号**。
    //
    //   踩过的坑: 原来只 connect(sceneGraphInitialized) 再等 5 秒看标志位, 结果
    //   **必然误报**。原因: 本函数在 engine.load() 之后才被调用, 而 Main.qml 的
    //   ApplicationWindow 是 visible: true —— load 期间窗口就已 show 并完成首次
    //   渲染, 这个**一次性**信号在那一刻就发过了; 之后才 connect 的永远收不到,
    //   标志位恒为 false, 5 秒后必然打出"未初始化"。实测 10-10 / 10-12 / 10-13
    //   三次运行全部误报, 而界面其实是正常的(日志里连下面那条 GL 信息都没有,
    //   恰好印证信号从没被收到过)。
    //   isSceneGraphInitialized() 是 Qt 5.15 的 public API(qquickwindow.h:188),
    //   读的是真实状态, 与 connect 时机无关。
    if (win->isSceneGraphInitialized())
        icloud::log::info(QStringLiteral("[GPU] 场景图已初始化(无需等待)"));
    else
        icloud::log::info(QStringLiteral("[GPU] 图形后端探针已挂载, 等待首次渲染"));

    // 信号仍然挂着: 它真触发时顺手把 GL 信息打出来(只有 OpenGL/硬件后端才有)。
    QObject::connect(win, &QQuickWindow::sceneGraphInitialized, win, []() {
        QOpenGLContext *ctx = QOpenGLContext::currentContext();
        if (!ctx) {
            // 软件渲染后端正常走这里 —— 这不是错误, 别用 warn 刷屏。
            icloud::log::info(QStringLiteral(
                "[GPU] 场景图已初始化(软件渲染后端, 无 OpenGL 上下文)"));
            return;
        }
        QOpenGLFunctions *f = ctx->functions();
        auto glStr = [f](GLenum name) -> QString {
            const GLubyte *s = f->glGetString(name);
            return s ? QString::fromLatin1(reinterpret_cast<const char *>(s))
                     : QStringLiteral("?");
        };
        icloud::log::info(QStringLiteral("[GPU] vendor=%1")
                              .arg(glStr(GL_VENDOR)));
        icloud::log::info(QStringLiteral("[GPU] renderer=%1")
                              .arg(glStr(GL_RENDERER)));
        icloud::log::info(QStringLiteral("[GPU] version=%1")
                              .arg(glStr(GL_VERSION)));
    });

    // 5 秒后**真没起来**才算异常(白屏/窗口不显示), 给可操作的后端强制开关。
    // 环境变量按 **Qt6** 写: Qt6 用 RHI, QT_OPENGL/QT_ANGLE_PLATFORM 那套已经
    // 没了(ANGLE 后端在 Qt6 里被移除), 现在是 QSG_RHI_BACKEND。
    QTimer::singleShot(5000, win, [win]() {
        if (win->isSceneGraphInitialized())
            return;
        icloud::log::warn(QStringLiteral(
            "[GPU] 5 秒内场景图未初始化 —— 若界面白屏/窗口不显示, "
            "多为图形后端在当前显卡/驱动上初始化失败。"
            "可尝试用环境变量强制后端: QSG_RHI_BACKEND=d3d11 "
            "(或 opengl / software) 或 QSG_RENDER_LOOP=basic"));
    });
}

// 应用图标。
//
// 用打包进 qrc 的矢量 iCloud 标志(来源见 resources/icloud.svg 顶部注释),
// 由 qsvg 图片插件栅格化 —— windeployqt 已把 imageformats/qsvg.dll 与
// Qt5Svg.dll 一起部署, 所以运行时不需要额外依赖。
//
// 用 SVG 而不是预渲染的 .ico: 一张矢量图覆盖任务栏/ Alt-Tab / 托盘 /
// 高 DPI 全部尺寸, 不必手工准备 16/32/48/256 四份位图。
QIcon appIcon()
{
    QIcon icon(QStringLiteral(":/icons/icloud.svg"));
    // pixmap() 是真正的探针: QIcon 构造成功不代表渲染器可用,
    // 只有能取到像素才算图标真的能用。
    if (!icon.pixmap(32, 32).isNull())
        return icon;

    icloud::log::warn(QStringLiteral(
        "[RUN] iCloud 图标渲染失败(qsvg 插件缺失?), 回落到内置绘制图标"));

    // 兜底: 运行时画一个圆点 + "iC"。
    // 不能返回空 QIcon —— Windows 上没有托盘图标会导致系统通知弹不出来。
    QPixmap pm(32, 32);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x3e, 0x82, 0xf4));
    p.drawEllipse(2, 2, 28, 28);
    QFont f = p.font();
    f.setPixelSize(17);
    f.setBold(true);
    p.setFont(f);
    p.setPen(QColor(0xff, 0xff, 0xff));
    p.drawText(pm.rect(), Qt::AlignCenter, QStringLiteral("iC"));
    p.end();
    return QIcon(pm);
}

// ══════════════════════════════════════════════════════════════════════
//  界面截图钩子 (开发/自检用)
//
//  为什么需要它: 本机这个会话没有交互式桌面, 正常启动时 QML 窗口从不真正
//  渲染([GPU] 探针只有"已挂载"、没有 renderer 那几行就是证据)。于是任何
//  界面改动都只能"编译通过"而无法核对视觉效果。这个钩子把真实界面渲染成
//  PNG, 让视觉问题在交付前就能被发现, 而不是等使用者看到。
//
//  用法(不需要交互式桌面, 实测在本机可直接跑通):
//    set ICLOUDMON_SHOT=C:\tmp\ui.png
//    set ICLOUDMON_SHOT_PAGES=home,drive,settings     (留空则只截当前页)
//    iCloudMon.exe
//  会在每个页面上抓一张 ui-<page>.png。
//
//  如果所在会话拿不到 OpenGL 上下文(探针会记 "[GPU] 无当前 OpenGL 上下文"),
//  再加 QT_QUICK_BACKEND=software 走软件渲染即可出图。
//  不设 ICLOUDMON_SHOT 时整个钩子不生效, 对正常运行零影响(仅一次 getenv)。
// ══════════════════════════════════════════════════════════════════════
class ShotRunner : public QObject
{
public:
    ShotRunner(QQuickWindow *win, QString base, QStringList pages,
               QGuiApplication *app)
        : QObject(app), m_win(win), m_base(std::move(base)),
          m_pages(std::move(pages)), m_app(app) {}

    void start(int delayMs)
    {
        icloud::log::info(QStringLiteral("[SHOT] %1ms 后导出 %2 个页面 -> %3")
                              .arg(delayMs).arg(m_pages.size()).arg(m_base));
        QTimer::singleShot(delayMs, this, &ShotRunner::step);
    }

private:
    void step();
    QQuickWindow   *m_win;
    QString         m_base;
    QStringList     m_pages;
    int             m_i = 0;
    QGuiApplication *m_app;
};

void ShotRunner::step()
{
    if (m_i >= m_pages.size()) {
        icloud::log::info(QStringLiteral("[SHOT] 全部完成, 退出"));
        m_app->quit();
        return;
    }

    // 空字符串 = 不切页, 就截当前页
    const QString page = m_pages.at(m_i);
    if (!page.isEmpty())
        m_win->setProperty("currentKey", page);

    // 切页后等布局稳定再抓: StackLayout 的子项有 Behavior 动画,
    // 抓太早会拍到动画中间态。
    QTimer::singleShot(1400, this, [this, page]() {
        const QImage img = m_win->grabWindow();
        QString out = m_base;
        if (!page.isEmpty()) {
            out.replace(QStringLiteral(".png"),
                        QStringLiteral("-") + page + QStringLiteral(".png"));
        }
        if (img.isNull()) {
            icloud::log::warn(QStringLiteral("[SHOT] %1: grabWindow 返回空图")
                                  .arg(page.isEmpty() ? QStringLiteral("当前页") : page));
        } else if (!img.save(out)) {
            icloud::log::warn(QStringLiteral("[SHOT] %1: 保存失败").arg(out));
        } else {
            icloud::log::info(QStringLiteral("[SHOT] %1 -> %2 (%3x%4)")
                                  .arg(page.isEmpty() ? QStringLiteral("当前页") : page,
                                       out)
                                  .arg(img.width())
                                  .arg(img.height()));
        }
        ++m_i;
        step();
    });
}

void setupScreenshotHook(QQmlApplicationEngine &engine, QGuiApplication &app)
{
    const QByteArray path = qgetenv("ICLOUDMON_SHOT");
    if (path.isEmpty())
        return;  // 正常运行走这条, 零开销

    if (engine.rootObjects().isEmpty()) {
        icloud::log::warn(QStringLiteral("[SHOT] 根对象为空, 无法截图"));
        return;
    }
    auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    if (!win) {
        icloud::log::warn(QStringLiteral("[SHOT] 根对象不是 QQuickWindow"));
        return;
    }

    QStringList pages;
    const QByteArray raw = qgetenv("ICLOUDMON_SHOT_PAGES");
    if (!raw.isEmpty())
        pages = QString::fromLocal8Bit(raw).split(QLatin1Char(','),
                                                   Qt::SkipEmptyParts);
    if (pages.isEmpty())
        pages << QString();

    // ICLOUDMON_SHOT_W / _H: 截图前把窗口改成指定尺寸。
    //
    // 为什么需要: 布局类缺陷(控件被裁、长文本把按钮挤出可视区)**只在
    // 特定宽度下出现**。默认 1100 宽时一切正常, 用户把窗口缩到最小宽度
    // 才发现问题 —— 没有这个开关就只能靠用户描述, 无法稳定复现与回归验证。
    // 复现"缩窄后刷新按钮被裁"就是靠它。
    if (!qEnvironmentVariableIsEmpty("ICLOUDMON_SHOT_W")) {
        const int w = qEnvironmentVariableIntValue("ICLOUDMON_SHOT_W");
        const int h = qEnvironmentVariableIntValue("ICLOUDMON_SHOT_H");
        if (w > 0) {
            win->resize(w, h > 0 ? h : win->height());
            // resize 只是请求, 场景图要下一帧才生效, 截图钩子本身有
            // 2500ms 起始延时, 足够布局稳定。
        }
    }

    auto *runner = new ShotRunner(win, QString::fromLocal8Bit(path), pages, &app);
    runner->start(2500);  // 给首次渲染与数据采样留时间
}

// ══════════════════════════════════════════════════════════════════════
//  一次性迁移: 旧版把 QCoreApplication 组织名写成了作者的账号名 zvozve,
//  于是注册表根与 %APPDATA% 目录都带上了这个名字。v1.0.4 起统一改为产品名
//  iCloudMon, 老用户首次运行新版时把数据搬过去, 只搬一次。
//
//  ★ 只动**本程序自己**的两处数据, 绝不触碰 iCloud 官方客户端的任何东西:
//      %APPDATA%\zvozve\iCloudMon\      -> %APPDATA%\iCloudMon\iCloudMon\
//      HKCU\Software\zvozve\iCloudMon   -> HKCU\Software\iCloudMon\iCloudMon
//    前者存的是卡死检测历史(icloud_state.jsonl), 后者是用户设置(导航顺序、
//    UI 开关、同步根覆盖值……)。
//
//  幂等: 仅当「老位置有内容 且 新位置为空」时才搬; 搬完清掉老位置。
//  这样重复运行不会覆盖用户在**新版**里已改过的设置。
//
//  必须在 log::init() 之前调用(QSettings 不依赖日志), 返回一条说明供
//  log::init() 之后写进日志 —— 迁移结果必须留痕, 否则出问题无从追溯。
// ══════════════════════════════════════════════════════════════════════
QString migrateLegacyUserData()
{
    QStringList notes;

    // ── ① 注册表: HKCU\Software\zvozve\iCloudMon → ...\iCloudMon\iCloudMon ──
    // QSettings 的 (scope, org, app) 构造显式传入 org/app, 不依赖
    // setOrganizationName 的当前值, 所以两边都能精确指向。
    QSettings oldReg(QSettings::NativeFormat, QSettings::UserScope,
                     QStringLiteral("zvozve"), QStringLiteral("iCloudMon"));
    QSettings newReg(QSettings::NativeFormat, QSettings::UserScope,
                     QStringLiteral("iCloudMon"), QStringLiteral("iCloudMon"));
    const QStringList oldKeys = oldReg.allKeys();
    if (!oldKeys.isEmpty() && newReg.allKeys().isEmpty()) {
        for (const QString &k : oldKeys)
            newReg.setValue(k, oldReg.value(k));
        newReg.sync();
        if (newReg.status() == QSettings::NoError) {
            oldReg.clear();          // 清掉老键值, 避免下次再搬
            oldReg.sync();
            // ★ QSettings::clear() 只清 <org>\<app> 下的键值, **不会删掉
            //   空的 org 父键** —— 实测清完还剩一个空壳 HKCU\Software\zvozve。
            //   要彻底抹掉这个名字, 得用原生 API:
            //     · 先删子树 ...\zvozve\iCloudMon;
            //     · 再删空的 ...\zvozve —— RegDeleteKeyW **只在无子键时成功**,
            //       所以万一 zvozve 下还有别的程序留下的键, 这一步会安全地
            //       失败(返回非 0), 不会误删别人的东西。
            ::RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\zvozve\\iCloudMon");
            ::RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\zvozve");
            notes << QStringLiteral("注册表 zvozve→iCloudMon: 已迁移 %1 项")
                         .arg(oldKeys.size());
        } else {
            notes << QStringLiteral("注册表迁移写入失败(status=%1), 旧键保留")
                         .arg(int(newReg.status()));
        }
    }

    // ── ② 历史文件目录: %APPDATA%\zvozve\iCloudMon → ...\iCloudMon\iCloudMon ──
    const QString appData = qEnvironmentVariable("APPDATA");
    if (!appData.isEmpty()) {
        QDir oldDir(appData + QStringLiteral("/zvozve/iCloudMon"));
        const QDir newDir(appData + QStringLiteral("/iCloudMon/iCloudMon"));
        if (oldDir.exists()) {
            QDir().mkpath(newDir.absolutePath());
            const QStringList files = oldDir.entryList(QDir::Files);
            if (newDir.entryList(QDir::Files).isEmpty() && !files.isEmpty()) {
                int moved = 0;
                for (const QString &f : files) {
                    if (QFile::copy(oldDir.absoluteFilePath(f),
                                    newDir.absoluteFilePath(f))) {
                        QFile::remove(oldDir.absoluteFilePath(f));
                        ++moved;
                    }
                }
                notes << QStringLiteral("历史目录 zvozve→iCloudMon: 已迁移 %1 个文件")
                             .arg(moved);
            }
            // 老目录里文件已搬空(或本来就是空的) → 删掉空壳
            if (oldDir.entryList(QDir::Files).isEmpty())
                oldDir.removeRecursively();
            // ★ 父目录 (%APPDATA%\zvozve) 也可能变空 —— 不删的话用户 AppData
            //   下会长期留一个 zvozve 空目录, 等于改名没改干净(注册表那边
            //   同理, 用 RegDeleteKeyW 删空 org 键)。
            QDir orgDir(appData + QStringLiteral("/zvozve"));
            if (orgDir.exists()
                && orgDir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty())
                orgDir.removeRecursively();
        }
    }

    return notes.join(QStringLiteral("; "));
}

}  // namespace

int main(int argc, char *argv[])
{
    // ★ 这两个属性必须在 QGuiApplication 构造**之前**设置。
    // 原来写在构造之后 —— Qt 会忽略并只在 stderr 上警告一句, 而 GUI 程序
    // 没有控制台, 这条警告根本看不到(那时 Qt 消息处理器也还没装上)。
    // 后果是 125%/150% 缩放的屏幕上界面被拉伸发虚。
    QGuiApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QGuiApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

    // ★ 图形后端: 默认用 ANGLE(D3D11) 提供 OpenGL, 绕开失效的桌面 GL。
    //
    // 实测本机(日志证据): Qt 解析到 OpenGL 后端, 但场景图 5 秒都没初始化,
    // 表现是**白屏 + 窗口只出现在任务栏预览**(窗口建了但渲染不出来)。
    // 这是桌面 OpenGL 在部分显卡/驱动上失效的典型症状, 与 QML 无关。
    //
    // 做法: 保留 OpenGL 场景图(最成熟、QtQuick 控件全兼容), 只把底层 GL
    // 实现换成 ANGLE(OpenGL-on-D3D11), 兼容性显著更好。
    // ⚠ 这里**不是** setSceneGraphBackend("angle") —— angle 不是场景图后端名,
    //   场景图后端只有 opengl/software/d3d11/vulkan/metal; angle 是 QT_OPENGL
    //   的取值(即"用 ANGLE 提供 GL")。两者别混。
    //
    // qputenv 必须在 QGuiApplication 构造**之前**(QT_OPENGL 在创建第一个 GL
    // 上下文时被读取)。用户已用 QT_OPENGL/QT_QUICK_BACKEND 指定过时一律尊重,
    // 不强加, 方便个别机器仍想试别的后端。
    // ★ 图形后端: **默认软件渲染**。
    //
    // 实测本机两条 GPU 路线都不可用(日志 + 截图证据):
    //   · 桌面 OpenGL: 场景图 5 秒未初始化 → 白屏 + 窗口只在任务栏预览;
    //   · ANGLE(D3D11): 能出画面, 但数据刷新时出现**彩色花屏**(GPU 伪影)。
    // 而本程序是低动态监控界面(静态文字 + 5s 轮询刷新, 无持续动画),
    // **正确性远比流畅度重要** —— 花屏/白屏是不可用的, 掉几帧无所谓。
    // 所以默认走软件渲染(QT_QUICK_BACKEND=software), 该路径经全页面截图验证无伪影。
    //
    // 用户若确认自己机器 GPU 正常, 想换回硬件加速, 设以下任一环境变量即可覆盖:
    //   · QT_QUICK_BACKEND=opengl   (或 desktop)
    //   · QT_OPENGL=desktop|angle
    if (qEnvironmentVariableIsEmpty("QT_OPENGL")
        && qEnvironmentVariableIsEmpty("QT_QUICK_BACKEND")) {
        qputenv("QT_QUICK_BACKEND", "software");
    }

    // ★ 系统通知的通道选择(记录教训, 别再走回头路):
    //
    //   曾经用 QSystemTrayIcon::showMessage —— 在本机**死锁**: 点"测试通知"
    //   精确卡在那一句(日志里它之前的行有、之后的行没有, 进程只能强杀)。
    //   Qt 在 Windows 上这条路径最终落到系统的通知通道上, 而调用线程的 COM
    //   单元与通知通道要求的单元不一致时, 跨单元 marshal 会把调用方挂住。
    //
    //   于是先退成了自绘置顶浮层(Toast.qml)。但那根本不是系统通知: 不进通知
    //   中心、错过就没了、也不像"系统在提醒你"。现改为**自建专属 STA 线程
    //   发 WinRT Toast**(inc/SystemNotifier.h): 用 SDK 自带的 WinRT ABI 头
    //   直接调 Toast 接口(不依赖 WRL/C++/WinRT), 线程内 RoInitialize
    //   (SINGLE_THREADED) + 消息泵, UI 线程只入队唤醒、绝不阻塞。
    //
    //   实测(2026-10-13, 32 位 Release): Show() 返回 S_OK, 通知确实进了
    //   Windows 通知中心; 本程序没有注册开始菜单快捷方式也能正常弹出。

    // ★ 这里必须是 QApplication(QtWidgets), 不能是 QGuiApplication。
    //
    // 历史原因: 本程序界面全在 QML, 起初用 QGuiApplication 完全够用 ——
    // QSystemTrayIcon 虽属 QtWidgets 模块, 但它本身是 QObject, 不建 QWidget,
    // 所以能在 QGuiApplication 下正常工作。
    // 但托盘右键菜单只能通过 QSystemTrayIcon::setContextMenu(QMenu*) 提供,
    // 而 **QMenu 是货真价实的 QWidget** —— 没有 QApplication 实例时构造它会
    // 直接致命错误: "QWidget: Cannot create a QWidget without QApplication"。
    // 实测踩过: 用 QGuiApplication + new QMenu() 启动即挂(exit=127)。
    //
    // QApplication 是 QGuiApplication 的子类, 纯 QML 程序用它没有任何副作用;
    // CMake 里本来就已经因为 QSystemTrayIcon 链接了 Qt5::Widgets。
    // 顺带的好处: 托盘菜单是 Windows 原生菜单, 观感/交互与系统一致。
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("iCloudMon"));
    // 刻意**不**设 setApplicationDisplayName: 它在部分 Windows 环境下会被追加到
    // 窗口标题栏, 而 QML 的 title 已含"iCloud 状态监控", 叠加后标题重复成
    // "iCloud 状态监控 v1.0.1 - 同步中 - iCloud 状态监控"。窗口标题由 QML 唯一提供。
    // ★ 组织名 = 出品方标识, 会决定注册表根(HKCU\Software\<org>\<app>)与
    //   %APPDATA%\<org>\<app>\。它曾经是 zvozve(作者个人账号名) —— 对通用
    //   软件是错的: 用户机器的注册表里出现作者 handle 没有任何信息量, 别人
    //   fork 后还会跟自己抢同一个键。现统一为产品名 iCloudMon。
    app.setOrganizationName(QStringLiteral("iCloudMon"));
    // 版本号来自 CMake 的 project(VERSION), 编译期注入(单一真相源),
    // 不再手写字符串 —— 改版本只改 CMakeLists.txt 一处。
    app.setApplicationVersion(QStringLiteral(APP_VERSION));

    // ★ 关闭"最后一个窗口就退出"的默认行为。
    //
    // 设置页可以切换关闭按钮的行为(退出 / 关闭窗口后台常驻)。选"后台常驻"
    // 时窗口会被 hide() 掉 —— 而 hide 掉最后一个可见窗口, Qt 默认会直接
    // 结束进程, 开关就形同虚设。所以必须关掉这个默认, 由 QML 的 onClosing
    // 显式决定"隐藏"还是"退出"。
    // 副作用: 关闭分支必须自己调 Qt.quit(), 不能再指望 Qt 兜底 —— 见 Main.qml。
    app.setQuitOnLastWindowClosed(false);

    // 强制 Fusion 风格, 保证 QML 原生控件在各平台观感一致
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    // ── 全局统一字体 ──
    // 不设的话 Qt 逐控件挑字体, 同一个汉字在不同 Label/Button/ComboBox 里
    // 可能落到不同的回退字体(有的控件用 Segoe UI 的西文回退, 有的直接用
    // 系统 CJK 字体), 观感就是"汉字字体不统一"。
    // setFamilies 给的是**回退链**: 西文走 Segoe UI, 中文/日文自动落到
    // 微软雅黑。Qt 会按字符逐个在链上找第一个有该字形的字体, 于是全站
    // 汉字必然是同一款, 不会再各控件各挑。
    QFont appFont = app.font();
    appFont.setFamilies({ QStringLiteral("Segoe UI"),
                          QStringLiteral("Microsoft YaHei UI"),
                          QStringLiteral("Microsoft YaHei"),
                          QStringLiteral("SimHei") });
    app.setFont(appFont);

    // ── 旧版用户数据迁移 ──
    // 必须在任何 QSettings / %APPDATA% 读取之前: 否则本进程会先按新路径
    // 读到空配置(表现为"设置全丢了"), 再迁移也救不回这一轮的内存状态。
    // 结果先存着, 等 log::init() 之后写进日志。
    const QString migrationNote = migrateLegacyUserData();

    // ── 日志初始化 (必须在任何其他模块之前) ──
    icloud::log::init();
    icloud::log::info(QStringLiteral("[RUN] ==== iCloudMon %1 启动 ====")
                          .arg(app.applicationVersion()));
    if (!migrationNote.isEmpty())
        icloud::log::info(QStringLiteral("[RUN] 用户数据迁移: %1").arg(migrationNote));
    icloud::log::info(QStringLiteral("[RUN] Qt %1 / exe=%2")
                          .arg(QString::fromLatin1(qVersion()),
                               QCoreApplication::applicationDirPath()));

    // Qt 消息转发必须在 log::init 之后, 否则前面的日志无处可去
    routeQtMessagesToLog();

    // ══════════════════════════════════════════════════════════════
    //  单实例模式
    //
    //  没有它会怎样: 用户(尤其绿色单文件版)再双击一次就多开一个进程 →
    //  多个窗口、多个托盘图标、多份数据库轮询/复制, 纯属浪费还互相干扰。
    //
    //  经典 QLocalServer/QLocalSocket 方案(跨平台, 无需互斥体):
    //    1) 先尝试连接一个命名本地服务。连得上 => 已有实例在跑 => 发一条
    //       "show" 指令后**立刻退出**, 根本不建窗口(快);
    //    2) 连不上 => 自己就是首个实例, 清掉可能残留的失效 socket 后起服务;
    //    3) 之后任何实例连进来, 都把已有窗口 show/raise/activate 到前台,
    //       用户感知就是"双击了一下窗口就出来了"。
    //
    //  socket 名字带用户名: 同一台机器上多个用户各跑一份互不干扰。
    // ★ 名字里曾带 zvozve(作者标识), 换掉 —— 单实例名是运行时命名空间,
    //   别人 fork 后应各用各的名字, 不该跟着作者的 handle 走。
    const QString sockName =
        QStringLiteral("iCloudMon-") + qEnvironmentVariable("USERNAME");
    QLocalSocket probe;
    probe.connectToServer(sockName);
    if (probe.waitForConnected(300)) {
        probe.write("show");
        probe.flush();
        probe.waitForBytesWritten(300);
        icloud::log::info(QStringLiteral(
            "[RUN] 检测到已有实例运行, 已请求激活它并退出本次启动"));
        icloud::log::shutdown();
        return 0;                    // 已有实例负责显示, 本进程不建窗口
    }
    // 上次崩溃/被强杀会留下失效 socket, 不清掉 listen 会 AddressInUse 失败
    QLocalServer::removeServer(sockName);
    auto *instanceServer = new QLocalServer(&app);
    if (!instanceServer->listen(sockName)) {
        // listen 失败不至于致命(退化成可多开), 只记一条便于事后判断
        icloud::log::warn(QStringLiteral("[RUN] 单实例服务监听失败(%1), "
                                          "可能允许重复运行")
                              .arg(int(instanceServer->serverError())));
    }

    // 窗口图标 = 任务栏 / Alt-Tab 上显示的图标。
    // 放在 log::init 之后: appIcon() 渲染失败时要能写进日志文件。
    // 注意: QML 侧不再单独设 ApplicationWindow.icon —— QGuiApplication 这一处
    // 覆盖所有顶层窗口, 两处都设反而会形成两个真相源。
    const QIcon icon = appIcon();
    app.setWindowIcon(icon);
    icloud::log::info(QStringLiteral("[RUN] 应用图标: 32px 位图取用%1")
                          .arg(icon.pixmap(32, 32).isNull()
                                   ? QStringLiteral("失败")
                                   : QStringLiteral("正常")));

    addPluginPaths();

    QString drvErr;
    if (preloadSqliteDriver(&drvErr)) {
        icloud::log::info(QStringLiteral("[RUN] SQLite 驱动就绪 (%1)")
                              .arg(QSqlDatabase::drivers().join(
                                       QLatin1Char(','))));
    } else {
        icloud::log::critical(QStringLiteral("[RUN] SQLite 驱动加载失败: %1")
                                   .arg(drvErr));
    }

    // ══════════════════════════════════════════════════════════════
    //  ★ 声明顺序 = 析构顺序的倒序, 这里必须是 bridge 在前、engine 在后
    //
    //  C++ 规则: 栈对象按声明顺序的**倒序**析构。所以先声明的后死。
    //  engine 析构时会销毁全部 QML 对象, 而它们持有 Bridge 的引用
    //  (context property + 各种绑定)。如果 Bridge 先被析构, engine 收尾时
    //  碰到的是已销毁对象 —— 表现为 main 返回前后的一次段错误(SIGSEGV),
    //  而且日志里看不到任何线索(那一刻 log 已 shutdown)。
    //
    //  原来的顺序是 engine 在前、bridge 在后, 正好反了。实测:
    //  调整前正常关窗退出也是段错误, 调整后退出码为 0。
    // ══════════════════════════════════════════════════════════════
    icloud::Bridge bridge;
    QQmlApplicationEngine engine;

    // 桥接对象注册为上下文属性, QML 全局可用
    engine.rootContext()->setContextProperty(
        QStringLiteral("Bridge"), &bridge);
    engine.rootContext()->setContextProperty(
        QStringLiteral("AppVersion"), app.applicationVersion());

    // 系统通知(卡死告警 / 测试通知 / 关窗后台常驻提示)送到 Windows 通知中心。
    // 它自带专属 STA 线程, 这里只是挂线, 不阻塞界面。
    //
    // ★ 声明位置在 engine **之后**: 栈对象逆序析构, 所以它会比 QML 引擎先死。
    //   通知线程必须在 QML 树与 QApplication 收尾之前停干净 —— 退出阶段
    //   多留一个活线程, 就是往本程序历史上那个退出段错误上再叠一层变量。
    icloud::SystemNotifier notifier;
    QObject::connect(&bridge, &icloud::Bridge::systemNotify, &notifier,
                     [&notifier](const QString &title, const QString &detail) {
                         notifier.notify(title, detail);
                     });
    QObject::connect(&notifier, &icloud::SystemNotifier::availabilityChanged,
                     &bridge, &icloud::Bridge::setSystemNotifyAvailable);
    // ★ 工作线程在"就绪"之前就已经探完通道并发过 availabilityChanged,
    //   那一次在 connect 之前, 收不到 —— 所以这里主动同步一次当前值。
    bridge.setSystemNotifyAvailable(notifier.available());

    // 注意: 这里不注入 DriveRoot —— 同步根目录可被用户修改,
    // 注入的是启动快照, 改完不会刷新。统一走 Bridge.driveRoot 属性。

    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (engine.rootObjects().isEmpty()) {
        icloud::log::critical(
            QStringLiteral("[RUN] QML 根对象为空, 界面加载失败"));
        icloud::log::shutdown();
        return 1;
    }

    // 图形后端信息: 只有它出现后才能确定是不是软件渲染
    logGraphicsBackend(engine);

    // 主窗口裸指针(由 engine 拥有, 活到 app.exec() 返回), 后面多处要用:
    // 记标题、单实例激活、托盘菜单。engine 根对象为空时前面已 return 1,
    // 所以这里一定拿得到; 仍保留判空以防 qobject_cast 落空。
    auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());

    // 记录真实窗口标题(setApplicationDisplayName 在部分 Windows 环境下会被
    // 系统追加到窗口标题, 与 QML 的 title 叠加会造成标题重复)
    if (win)
        icloud::log::info(QStringLiteral("[RUN] 窗口标题: \"%1\"")
                              .arg(win->title()));

    // 单实例: 后续任何实例连进来, 把这个已存在的窗口调到前台。
    {
        QObject::connect(instanceServer, &QLocalServer::newConnection, &app,
                         [win, instanceServer]() {
            while (instanceServer->hasPendingConnections()) {
                QLocalSocket *s = instanceServer->nextPendingConnection();
                s->readAll();          // 读掉"show" 指令, 内容不重要
                s->deleteLater();
            }
            if (win) {
                win->show();
                win->raise();
                win->requestActivate();
            }
        });
    }

    // 开发自检用的截图钩子, 不设 ICLOUDMON_SHOT 时不做任何事
    setupScreenshotHook(engine, app);

    // ══════════════════════════════════════════════════════════════
    //  系统托盘
    //
    //  托盘承担两个职责:
    //    ① 常驻图标 —— 窗口关闭后(后台常驻模式)唯一的入口;
    //    ② 右键菜单: 显示主窗口 / 退出。**必须有** —— 没有它, 用户把窗口
    //       关掉后就没有任何正常途径重新打开或退出, 只能去任务管理器。
    //  卡死告警 / 测试通知走 Windows 系统通知(通知中心), 由 SystemNotifier
    //  在专属 STA 线程里发; 窗口在前台时 QML 再同步弹一条窗口内提醒
    //  (见 Main.qml 的 onSystemNotify)。
    // ══════════════════════════════════════════════════════════════
    QSystemTrayIcon *tray = nullptr;
    // 菜单在 if 外声明: 下面 aboutToQuit 里要释放它(QMenu 无 QWidget 父对象
    // 可挂, 又必须活得比托盘久, 所以在这里持有裸指针, 退出时手动 delete)。
    QMenu *trayMenu = nullptr;
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        tray = new QSystemTrayIcon(icon, &app);
        tray->setToolTip(QStringLiteral("iCloud 状态监控"));

        // 右键菜单
        trayMenu = new QMenu();
        QAction *actShow = trayMenu->addAction(QStringLiteral("显示主窗口"));
        trayMenu->addSeparator();
        QAction *actQuit = trayMenu->addAction(QStringLiteral("退出"));

        QObject::connect(actShow, &QAction::triggered, &app, [win]() {
            if (!win)
                return;
            win->show();
            win->raise();
            win->requestActivate();
        });
        QObject::connect(actQuit, &QAction::triggered, &app,
                         []() { QCoreApplication::quit(); });

        tray->setContextMenu(trayMenu);

        // 双击/单击托盘图标也唤起窗口(Windows 惯例是双击)。
        // QSystemTrayIcon::Trigger 覆盖左键单击与双击, 一致地把窗口调到前台。
        QObject::connect(tray, &QSystemTrayIcon::activated, &app,
                         [win](QSystemTrayIcon::ActivationReason reason) {
            if (!win)
                return;
            if (reason == QSystemTrayIcon::Trigger
                || reason == QSystemTrayIcon::DoubleClick) {
                win->show();
                win->raise();
                win->requestActivate();
            }
        });

        tray->show();
        icloud::log::info(
            QStringLiteral("[RUN] 系统托盘已启用(右键菜单: 显示主窗口/退出)"));
    } else {
        icloud::log::warn(QStringLiteral(
            "[RUN] 系统托盘不可用, 卡死告警只能记日志(不会弹通知)"));
    }

    // ★ 退出前必须显式 hide()。
    // Windows 的托盘实现(QWindowsSystemTrayIcon)内部自带一个 QThread 和
    // 一个隐藏的消息窗口; 直接退出时那个线程会先被销毁, 之后消息窗口再收到
    // 消息就会踩到已释放的对象 —— 表现为退出阶段的段错误(SIGSEGV),
    // 同时伴随日志里的 "Timers cannot be stopped from another thread"。
    // 实测: 不 hide 时正常关闭窗口也会崩; hide 后就干净了。
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app,
                     [tray, trayMenu, &notifier]() {
        // 通知线程要先停: 之后 app 里的东西开始拆, 线程不该再碰任何 Qt 对象
        notifier.shutdown();
        if (tray) {
            // ★ 先解除对菜单的引用再删菜单 —— 托盘内部保存着菜单裸指针,
            //   若让它析构时还指着已释放的菜单就是悬垂指针。
            tray->setContextMenu(nullptr);
            tray->hide();
        }
        delete trayMenu;   // 空指针 delete 安全
    });

    icloud::log::info(QStringLiteral("[RUN] 界面加载完成, 进入事件循环"));
    const int rc = app.exec();

    icloud::log::info(QStringLiteral("[RUN] ==== 退出 (code=%1) ====").arg(rc));
    icloud::log::shutdown();
    return rc;
}