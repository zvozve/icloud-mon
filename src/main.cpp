// ======================================================================
//  main.cpp  -  iCloud Drive 同步状态监控器
//
//  数据来源: iCloudDrive.db (SQLite), 只读。
//  界面: QML (Qt Quick Controls 2), 深色主题。
//  日志: spdlog, 按天切分, 输出到 exe 同级\log\
// ======================================================================
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QGuiApplication>
#include <QHash>
#include <QIcon>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include "Bridge.h"
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

    QObject::connect(win, &QQuickWindow::sceneGraphInitialized, win, []() {        QOpenGLContext *ctx = QOpenGLContext::currentContext();
        if (!ctx) {
            icloud::log::warn(QStringLiteral(
                "[GPU] 无当前 OpenGL 上下文 (可能不是 OpenGL 后端)"));
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
    // 挂载成功不代表会触发 —— 只有窗口真正渲染时才会发 sceneGraphInitialized。
    // 所以"有这行、没有下面的 renderer 行" = 窗口在本会话里从未渲染过
    // (无交互桌面/会话隔离), 而不是探针没装上。这条能把两种情况分开。
    icloud::log::info(QStringLiteral("[GPU] 图形后端探针已挂载, 等待首次渲染"));
}

}  // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("iCloudMon"));
    app.setApplicationDisplayName(QStringLiteral("iCloud 状态监控"));
    app.setOrganizationName(QStringLiteral("zvozve"));
    app.setApplicationVersion(QStringLiteral("1.0.0"));

    // 强制 Fusion 风格, 保证 QML 原生控件在各平台观感一致
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    // 高 DPI 适配 (Qt 5.15 默认已开, 显式声明便于移植到其他版本)
    QGuiApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QGuiApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

    // ── 日志初始化 (必须在任何其他模块之前) ──
    icloud::log::init();
    icloud::log::info(QStringLiteral("[RUN] ==== iCloudMon %1 启动 ====")
                          .arg(app.applicationVersion()));
    icloud::log::info(QStringLiteral("[RUN] Qt %1 / exe=%2")
                          .arg(QString::fromLatin1(qVersion()),
                               QCoreApplication::applicationDirPath()));

    // Qt 消息转发必须在 log::init 之后, 否则前面的日志无处可去
    routeQtMessagesToLog();

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

    QQmlApplicationEngine engine;

    // 桥接对象注册为上下文属性, QML 全局可用
    icloud::Bridge bridge;
    engine.rootContext()->setContextProperty(
        QStringLiteral("Bridge"), &bridge);
    engine.rootContext()->setContextProperty(
        QStringLiteral("AppVersion"), app.applicationVersion());
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

    icloud::log::info(QStringLiteral("[RUN] 界面加载完成, 进入事件循环"));
    const int rc = app.exec();

    icloud::log::info(QStringLiteral("[RUN] ==== 退出 (code=%1) ====").arg(rc));
    icloud::log::shutdown();
    return rc;
}