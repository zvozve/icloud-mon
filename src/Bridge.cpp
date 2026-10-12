#include "Bridge.h"

#include <QtMath>

#include <QDateTime>
#include <QElapsedTimer>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QUrl>

#include "IcloudPaths.h"
#include "Log.h"

namespace icloud {

namespace {

// ★ 打开目录/文件 —— 统一走 QDesktopServices, 不直接 explorer.exe。
//
// 为什么不用 `QProcess::startDetached("explorer.exe", { dir })`:
//   explorer.exe 对参数的解析很挑, 实测有两个坑:
//     ① 路径无法识别时**不报错, 而是静默回退打开默认目录(Documents)** ——
//        表现就是"点了云盘回收站, 跳到文档目录去了", 极难自查。
//     ② `/select,<路径>` 这种"参数与值连在一起"的写法要求中间无空格,
//        经QProcess 传数组后引号化行为不一致, 选中经常失效。
//   QDesktopServices 内部走 ShellExecute/ShellAPI, 路径按系统规则处理,
//   失败时返回 false 可以据实报错, 不会静默跑去别处。
bool openInShell(const QString &path)
{
    const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(path));
    const QUrl url = QUrl::fromLocalFile(clean);
    if (!QDesktopServices::openUrl(url)) {
        log::warn(QStringLiteral("打开失败(ShellAPI 拒绝): %1").arg(clean));
        return false;
    }
    return true;
}

}  // namespace

Bridge::Bridge(QObject *parent)
    : QObject(parent)
    , m_monitor(new IcloudMonitor(this))
    , m_service(new IcloudService(this))
    , m_autoStart(new AutoStart(this))
{
    connect(m_monitor, &IcloudMonitor::snapshotReady,
            this, &Bridge::onSnapshot);
    connect(m_monitor, &IcloudMonitor::stateChanged,
            this, &Bridge::onStateChanged);
    connect(m_monitor, &IcloudMonitor::anomalyDetected,
            this, &Bridge::onAnomaly);
    connect(m_monitor, &IcloudMonitor::anomalyCleared,
            this, &Bridge::onAnomalyCleared);

    connect(m_service, &IcloudService::finished,
            this, &Bridge::onIcloudFinished);
    connect(m_service, &IcloudService::progress,
            this, &Bridge::onIcloudProgress);

    // 侧边栏顺序与默认启动页(用户设置)
    loadNavSettings();
    // 窗口行为类设置(关闭按钮行为)
    loadUiSettings();

    // 进程状态轮询: 3s 一次, 比重启按钮的即时性好
    m_procTimer = new QTimer(this);
    m_procTimer->setInterval(3000);
    connect(m_procTimer, &QTimer::timeout, this, &Bridge::onProcTimer);
    m_procTimer->start();
    onProcTimer();

    startMonitoring();
}

Bridge::~Bridge() = default;

// ── 监控控制 ──────────────────────────────────────────────────────
void Bridge::startMonitoring()
{
    m_monitor->setStallSeconds(m_stallSec);
    m_monitor->start(m_refreshSec * 1000);
}

void Bridge::stopMonitoring()
{
    m_monitor->stop();
}

void Bridge::refreshNow()
{
    m_monitor->refresh();
}

// ── iCloud 控制 ───────────────────────────────────────────────────
void Bridge::restartIcloud()
{
    if (m_restarting)
        return;
    log::userAction(QStringLiteral("重启 iCloud"));
    m_restarting = true;
    emit icloudStateChanged();
    emit notify(QStringLiteral("info"), QStringLiteral("正在重启 iCloud..."));
    m_service->restart();
}

void Bridge::stopIcloud()
{
    if (m_restarting)
        return;
    log::userAction(QStringLiteral("停止 iCloud"));
    m_restarting = true;
    emit icloudStateChanged();
    m_service->stopAll();
}

void Bridge::startIcloud()
{
    if (m_restarting)
        return;
    log::userAction(QStringLiteral("启动 iCloud"));
    m_restarting = true;
    emit icloudStateChanged();
    m_service->startAll();
}

void Bridge::onIcloudFinished(bool ok, const QString &msg)
{
    log::userAction(QStringLiteral("重启 iCloud"), msg);
    m_restarting = false;
    emit icloudStateChanged();
    emit notify(ok ? QStringLiteral("ok") : QStringLiteral("error"), msg);
    onProcTimer();          // 立即刷新进程状态
    m_monitor->refresh();   // 重启后同步状态会变, 立刻采一次
}

void Bridge::onIcloudProgress(const QString &msg)
{
    emit notify(QStringLiteral("info"), msg);
}

void Bridge::onProcTimer()
{
    m_procList = m_service->runningProcesses();
    const bool now = !m_procList.isEmpty();
    if (now != m_icloudRunning) {
        m_icloudRunning = now;
        // 进程识别结果记一条: 既是运行记录, 也是"进程探测是否正常"的证据
        log::info(QStringLiteral("[RUN] iCloud 进程: %1")
                      .arg(m_procList.isEmpty()
                               ? QStringLiteral("无")
                               : m_procList.join(QLatin1Char(','))));
        emit icloudStateChanged();
    }
    // ★ 这里"只在状态变化时 emit"是**正确**的, 不用改。
    //   实测出现过的"文字说已停止、开关却停在打开"不是这里的问题, 而是
    //   QML 侧写了 `checked = Bridge.icloudRunning` —— 那是 JS 赋值,
    //   会把 `checked: Bridge.icloudRunning` 绑定整个删除(绑定被删后
    //   checked 永远停在用户最后拨到的值, 不再跟随真实状态)。
    //   Main.qml 里已去掉那行赋值。
}

// ── 自启动 ────────────────────────────────────────────────────────
bool Bridge::autoStart() const
{
    return m_autoStart->isEnabled();
}

bool Bridge::setAutoStart(bool on)
{
    QString err;
    if (!m_autoStart->setEnabled(on, &err)) {
        log::userAction(QStringLiteral("设置开机自启动=%1").arg(int(on)),
                        QStringLiteral("失败: %1").arg(err));
        emit notify(QStringLiteral("error"),
                    QStringLiteral("设置自启动失败: %1").arg(err));
        // ★ 必须把**真实值**再推一次。设置页的开关是
        //   `checked: Bridge.autoStart` 单向绑定, 失败时 UI 已经被用户
        //   拨动过了; 这里 emit autoStartChanged 让绑定把 checked 拉回
        //   真实状态, 完成回滚。
        //   (不能用 QML 侧 `checked = Bridge.autoStart` 回滚 —— 那是 JS
        //    赋值, 会把绑定删掉, 之后开关再也不跟随真实状态。)
        emit autoStartChanged();
        return false;
    }
    log::userAction(QStringLiteral("设置开机自启动=%1").arg(int(on)),
                    QStringLiteral("成功"));
    emit autoStartChanged();
    emit notify(QStringLiteral("ok"),
                on ? QStringLiteral("已开启开机自启动")
                   : QStringLiteral("已关闭开机自启动"));
    return true;
}

// ── 历史 ──────────────────────────────────────────────────────────
void Bridge::resetHistory()
{
    m_monitor->resetHistory();
    log::userAction(QStringLiteral("清空卡死检测基线"), QStringLiteral("成功"));
    emit notify(QStringLiteral("ok"), QStringLiteral("已清空历史基线"));
}

QString Bridge::stateFilePath() const
{
    return m_monitor->stateFilePath();
}

// 在资源管理器中定位一个路径。
//
// 三种情况:
//   1. path 本身是已存在的文件 -> explorer /select 选中它(并打开所在目录)
//   2. path 是已存在的目录     -> 直接打开
//   3. path 不存在(待处理项的文件可能还没落到本地) ->
//      逐级向上找**最近一个存在的祖先目录**并打开它。
//      找不到就只记日志 + 弹提示, 绝不崩 —— 这些路径来自数据库快照,
//      随时可能因为 iCloud 变动而失效。
void Bridge::openItemLocation(const QString &raw)
{
    if (raw.isEmpty()) {
        log::userAction(QStringLiteral("定位"), QStringLiteral("失败: 路径为空"));
        emit notify(QStringLiteral("warn"), QStringLiteral("该项没有可用路径"));
        return;
    }

    QString path = QDir::cleanPath(QDir::fromNativeSeparators(raw));
    QFileInfo fi(path);

    // 情况 1/2: 目标存在
    if (fi.exists()) {
        if (fi.isDir()) {
            log::userAction(QStringLiteral("打开目录"), path);
            if (!openInShell(path))
                emit notify(QStringLiteral("error"),
                            QStringLiteral("无法打开目录: %1").arg(path));
        } else {
            log::userAction(QStringLiteral("定位文件"), path);
            // explorer 的 /select, 后面**不能有空格**, 参数是一个整体。
            // 同样交给 ShellAPI: 直接 openUrl 会打开文件而不是"选中"它,
            // 所以这里仍走 explorer, 但用 QProcess::splitCommand 的
            // 单字符串形式自己拼, 避免 QProcess 各自引号化破坏整体性。
            const QString cmd = QStringLiteral("explorer.exe /select,")
                                + QDir::toNativeSeparators(path);
            if (!QProcess::startDetached(cmd))
                openInShell(path);      // 退一步: 至少把所在目录打开
        }
        return;
    }

    // 情况 3: 向上找最近的存在目录
    QString dir = fi.absolutePath();
    while (!dir.isEmpty()) {
        if (QDir(dir).exists()) {
            log::userAction(QStringLiteral("定位(目标不存在, 打开最近上级目录)"),
                            QStringLiteral("%1 -> %2").arg(path, dir));
            emit notify(QStringLiteral("warn"),
                        QStringLiteral("目标尚未落到本地，已打开所在目录"));
            openInShell(dir);
            return;
        }
        const QString parent = QDir(dir).absolutePath();
        if (parent == dir)   // 到盘根了还没找到
            break;
        dir = parent;
    }

    log::userAction(QStringLiteral("定位"), QStringLiteral("失败: %1").arg(path));
    emit notify(QStringLiteral("error"),
                QStringLiteral("找不到该文件所在目录: %1").arg(path));
}

// 回收站项: 打开被删除文件原来所在的目录。
// 刻意不跳 openItemLocation —— 那边会对"存在的文件"做 /select, 而回收站项
// 的文件早已被删除(原路径下没有它), 直接打开原目录 + 据实提示才对。
void Bridge::openTrashLocation(const QString &originalPath)
{
    if (originalPath.isEmpty()) {
        log::userAction(QStringLiteral("定位回收站项"),
                        QStringLiteral("失败: 原路径为空"));
        emit notify(QStringLiteral("warn"), QStringLiteral("该项没有记录原路径"));
        return;
    }

    const QString file =
        QDir::cleanPath(QDir::fromNativeSeparators(originalPath));
    // 取父目录 = 文件删除前所在的位置
    QString dir = QFileInfo(file).absolutePath();
    while (!dir.isEmpty()) {
        if (QDir(dir).exists()) {
            log::userAction(QStringLiteral("打开回收站项原位置"),
                            QStringLiteral("%1 -> %2").arg(file, dir));
            emit notify(QStringLiteral("ok"),
                        QStringLiteral("文件已删除，已打开其原所在目录:\n%1").arg(dir));
            openInShell(dir);
            return;
        }
        const QString parent = QDir(dir).absolutePath();
        if (parent == dir)
            break;
        dir = parent;
    }

    log::userAction(QStringLiteral("定位回收站项"),
                    QStringLiteral("失败: %1").arg(file));
    emit notify(QStringLiteral("error"),
                QStringLiteral("找不到该文件原来的目录: %1").arg(file));
}

// 用系统默认浏览器打开 URL(仓库链接)。
void Bridge::openUrl(const QString &url)
{
    const QUrl u = QUrl::fromUserInput(url);   // 容错: 没写 scheme 也能识别
    if (!u.isValid() || u.scheme().isEmpty()) {
        log::userAction(QStringLiteral("打开链接"),
                        QStringLiteral("失败: URL 无效 %1").arg(url));
        emit notify(QStringLiteral("error"), QStringLiteral("链接无效"));
        return;
    }
    log::userAction(QStringLiteral("打开链接"), u.toString());
    QDesktopServices::openUrl(u);
}

void Bridge::openLogDir()
{
    const QString dir = log::logDir();
    if (dir.isEmpty() || !QDir(dir).exists()) {
        log::warn(QStringLiteral("日志目录不存在: %1").arg(dir));
        emit notify(QStringLiteral("error"), QStringLiteral("日志目录不存在"));
        return;
    }
    log::userAction(QStringLiteral("打开日志目录"), dir);
    // 统一走 ShellAPI —— explorer.exe 解析不了参数时会静默回退到
    // 默认目录(Documents), 表现为"点了没去日志目录, 跳到文档去了"。
    openInShell(dir);
}

// ── 同步根目录 ────────────────────────────────────────────────────
QString Bridge::driveRoot() const
{
    return paths::driveRoot();
}

void Bridge::openDriveRoot()
{
    const QString dir = paths::driveRoot();
    if (!QDir(dir).exists()) {
        log::warn(QStringLiteral("同步根目录不存在: %1").arg(dir));
        emit notify(QStringLiteral("error"),
                    QStringLiteral("同步根目录不存在: %1").arg(dir));
        return;
    }
    log::userAction(QStringLiteral("打开 iCloud 目录"), dir);
    openInShell(dir);
}

void Bridge::openCloudTrash()
{
    // iCloud Drive 的回收站是同步根下的隐藏目录 .Trash。
    // (实测本机: C:\Container\Cloud\iCloudDrive\.Trash 确实存在)
    // ★ 只"打开查看", 本程序不提供任何删除/清空动作 —— 删除是不可逆的,
    //   不该由一个监控程序代劳。
    const QString dir = paths::driveRoot() + QStringLiteral("/.Trash");
    if (!QDir(dir).exists()) {
        log::warn(QStringLiteral("iCloud 回收站不存在: %1").arg(dir));
        emit notify(QStringLiteral("info"),
                    QStringLiteral("iCloud 回收站还不存在（还没有删除过文件）"));
        return;
    }
    log::userAction(QStringLiteral("打开 iCloud 回收站"), dir);
    // ★ 这里以前是 `QProcess::startDetached("explorer.exe", { dir })`,
    //   结果**点了会跳到 Documents 目录**。
    //   原因: explorer.exe 收到 `.Trash` 这种以点开头的目录参数时
    //   解析失败, 而它**不报错、直接回退打开默认目录**, 静默得没法查。
    //   `.Trash` 里通常也没有可显示的内容(回收站条目在数据库里),
    //   打开一个空目录意义不大 —— 顺带在这里说清楚"回收站有多少项"。
    const QDir d(dir);
    const QStringList entries = d.entryList(QDir::AllEntries | QDir::NoDotAndDotDot);
    if (entries.isEmpty()) {
        emit notify(QStringLiteral("info"),
                    QStringLiteral("iCloud 回收站是空的（当前 0 项）\n目录: %1").arg(dir));
    }
    if (!openInShell(dir))
        emit notify(QStringLiteral("error"),
                    QStringLiteral("无法打开 iCloud 回收站: %1").arg(dir));
}

void Bridge::openSystemTrash()
{
    // 打开系统回收站。shell:RecycleBinFolder 是系统内置的文件夹标识,
    // explorer 认它, 不需要 SHFileOperation 那种会弹确认框的老 API。
    log::userAction(QStringLiteral("打开系统回收站"));
    QProcess::startDetached(QStringLiteral("explorer.exe"),
                             { QStringLiteral("shell:RecycleBinFolder") });
}

bool Bridge::setDriveRoot(const QString &path)
{
    const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(path));

    // 先校验再存: 存一个不存在的路径进去, 之后每次打开都会失败,
    // 而用户很难想到是自己存错了路径。
    QDir probe(clean);
    if (!probe.exists()) {
        log::userAction(QStringLiteral("设置同步根目录=%1").arg(clean),
                        QStringLiteral("失败: 目录不存在"));
        emit notify(QStringLiteral("error"),
                    QStringLiteral("目录不存在: %1").arg(clean));
        return false;
    }
    // iCloud Drive 根目录里必然有 .icloud 隐藏文件, 拿它做二次确认,
    // 避免用户随手选了个普通文件夹却以为设置成功了。
    const bool looksLikeDrive = !probe.entryList(
                    QStringList() << QStringLiteral(".icloud"),
                    QDir::Hidden | QDir::Files).isEmpty();

    QString err;
    if (!paths::setDriveRoot(clean, &err)) {
        log::userAction(QStringLiteral("设置同步根目录=%1").arg(clean),
                        QStringLiteral("失败: %1").arg(err));
        emit notify(QStringLiteral("error"),
                    QStringLiteral("保存失败: %1").arg(err));
        return false;
    }

    log::userAction(QStringLiteral("设置同步根目录=%1").arg(clean),
                    looksLikeDrive ? QStringLiteral("成功(检测到 .icloud)")
                                   : QStringLiteral("成功(未检测到 .icloud)"));
    emit driveRootChanged();
    emit notify(QStringLiteral("ok"), QStringLiteral("同步根目录已更新"));
    // 路径变了, 立刻重采一次让界面跟上
    m_monitor->refresh();
    return true;
}

void Bridge::resetDriveRoot()
{
    paths::clearDriveRoot();
    log::userAction(QStringLiteral("恢复默认同步根目录"),
                    paths::driveRoot());
    emit driveRootChanged();
    emit notify(QStringLiteral("ok"), QStringLiteral("已恢复默认路径"));
    m_monitor->refresh();
}

// ── 快照处理 ──────────────────────────────────────────────────────
void Bridge::onSnapshot(const icloud::Snapshot &s)
{
    m_snap = s;
    m_pendingModel.setItems(s.pendingItems);
    m_localModel.setItems(s.localPendings);
    m_trashModel.setItems(s.trashItems);
    refreshCategories();
    emit snapshotChanged();
}

void Bridge::onStateChanged(const icloud::Snapshot &s)
{
    m_snap = s;
    m_pendingModel.setItems(s.pendingItems);
    m_localModel.setItems(s.localPendings);
    m_trashModel.setItems(s.trashItems);
    refreshCategories();
    emit snapshotChanged();
}

    // ★ 当前**未启用**: 自研的 BPlist 解析器还没调通(Apple 这个偏好文件
    //   用的不是标准 bplist 编码: 整数是 0x1_、bool 是 0x08/0x09)。
    //   代码保留在 IcloudQuota.cpp / BPlist.cpp, 等解析器验证通过后再
    //   挂回这里并接上首页那张存储卡片。
    //   现在不调用 = 不会每 3 秒读文件、不会打无意义的解析失败警告。

void Bridge::onAnomaly(const QString &kind, const QString &title,
                       const QString &detail)
{
    // 注意: 卡死这类异常会**每采样一次上报一次**(每 5 秒), 不是只在
    // 出现的那一次上报。所以这里的 flag 必须在赋值前先取, 判断"是不是新异常"。
    const bool wasAnomalous = m_anomalous;
    const bool isNewKind = !m_anomalyList.contains(kind);

    m_anomalous = true;
    m_anomalyTitle = title;
    m_anomalyDetail = detail;
    if (isNewKind)
        m_anomalyList.append(kind);
    emit anomalyChanged();      // 明细(停滞秒数等)要跟着刷新, 每次都发

    // 但弹窗只在新出现 / 换了异常种类时给。
    // 每次都弹的话, 持续卡死状态下 Toast 动画会被每 5 秒重启一次,
    // 既是持续打扰, 也让界面反复重绘。
    if (!wasAnomalous || isNewKind) {
        // 卡死 -> 走置顶浮层系统通知(跨应用可见, 见 Toast.qml);
        // 其他异常 -> 走窗口内 Toast(notify)。两者分开, 避免一次卡死
        // 同时弹两条。
        if (kind == QStringLiteral("卡死")) {
            log::userAction(QStringLiteral("卡死告警触发系统通知"),
                            QStringLiteral("停滞 %1").arg(detail));
            emit systemNotify(title, detail);
        } else {
            emit notify(QStringLiteral("warn"),
                        QStringLiteral("[%1] %2").arg(kind, title));
        }
    }
}

void Bridge::testSystemNotification()
{
    log::userAction(QStringLiteral("测试系统通知"));
    emit systemNotify(
        QStringLiteral("iCloud 状态监控"),
        QStringLiteral("测试通知: 能弹出这条说明只要 iCloud 卡死, 你就能收到提醒。"));
}

// ── 四类同步 ──────────────────────────────────────────────────────
void Bridge::refreshCategories()
{
    // 相册/密码/书签要各复制并打开一个 SQLite 库, 比读云盘重。
    // 实测一次约 54ms, 跑在 GUI 线程上。这三类变化很慢, 没必要频繁查 ——
    // 每 6 个采样刷一次(默认约 30 秒), 把这点开销的占比压到 0.2% 以下。
    m_catTick++;
    if (!m_categories.isEmpty() && (m_catTick % 6) != 1)
        return;

    QElapsedTimer t;
    t.start();
    m_categories = IcloudCategories::readAll(m_snap);
    const qint64 ms = t.elapsed();

    // 只在"读得慢"时记耗时: 这段跑在 GUI 线程上, 慢了就是卡顿来源
    if (ms > 50)
        log::warn(QStringLiteral("[PERF] 四类状态读取偏慢 %1ms").arg(ms));

    // 四类状态变化时记一行 —— 这是看不到界面时验证数据链路的唯一手段
    QString sig;
    for (const CategoryStatus &c : m_categories) {
        sig += QStringLiteral("%1=%2(%3) ")
                   .arg(c.key,
                        c.enabled ? QStringLiteral("启用")
                                  : QStringLiteral("未启用"),
                        c.userSet ? QStringLiteral("手动")
                                  : QStringLiteral("自动"));
    }
    if (sig != m_lastCatSig) {
        m_lastCatSig = sig;
        log::info(QStringLiteral("[CAT] %1").arg(sig.trimmed()));
        for (const CategoryStatus &c : m_categories)
            log::info(QStringLiteral("[CAT] %1 摘要: %2")
                          .arg(c.name, c.summary));
    }

    emit categoriesChanged();
}

QVariantList Bridge::categories() const
{
    QVariantList out;
    out.reserve(m_categories.size());
    for (const CategoryStatus &c : m_categories) {
        QVariantList det;
        for (const CatDetail &d : c.details) {
            QVariantMap m;
            m.insert(QStringLiteral("label"), d.label);
            m.insert(QStringLiteral("value"), d.value);
            det.append(m);
        }
        QVariantMap m;
        m.insert(QStringLiteral("key"),      c.key);
        m.insert(QStringLiteral("name"),     c.name);
        m.insert(QStringLiteral("summary"),  c.summary);
        m.insert(QStringLiteral("enabled"),  c.enabled);
        m.insert(QStringLiteral("userSet"),  c.userSet);
        m.insert(QStringLiteral("autoOn"),   c.autoOn);
        m.insert(QStringLiteral("autoWhy"),  c.autoWhy);
        m.insert(QStringLiteral("hasData"),  c.hasData);
        m.insert(QStringLiteral("note"),     c.note);
        m.insert(QStringLiteral("details"),  det);
        out.append(m);
    }
    return out;
}

void Bridge::setCategoryEnabled(const QString &key, bool on)
{
    IcloudCategories::setUserValue(key, on);
    for (CategoryStatus &c : m_categories) {
        if (c.key == key) {
            c.userSet = true;
            c.enabled = on;
        }
    }
    emit categoriesChanged();
}

void Bridge::clearCategoryEnabled(const QString &key)
{
    IcloudCategories::clearUserValue(key);
    for (CategoryStatus &c : m_categories) {
        if (c.key == key) {
            c.userSet = false;
            c.enabled = c.autoOn;   // 回退到自动检测
        }
    }
    emit categoriesChanged();
}

// ── 侧边栏顺序 / 默认启动页 ───────────────────────────────────────
QString Bridge::categoryName(const QString &key) const
{
    for (const CategoryStatus &c : m_categories)
        if (c.key == key)
            return c.name;
    // 还没采到数据时的兜底, 保证顺序设置界面不会显示成 "drive"
    if (key == QStringLiteral("drive"))     return QStringLiteral("云盘");
    if (key == QStringLiteral("photos"))    return QStringLiteral("相册");
    if (key == QStringLiteral("passwords")) return QStringLiteral("密码");
    if (key == QStringLiteral("bookmarks")) return QStringLiteral("书签");
    return key;
}

void Bridge::loadNavSettings()
{
    QSettings s = paths::userSettings();

    const QStringList all = categoryKeys();
    QStringList order;
    for (const QString &k : s.value(QStringLiteral("nav/order")).toStringList())
        if (all.contains(k) && !order.contains(k))
            order << k;
    // 补齐缺失项(例如以后新增了类别), 否则该项会在侧边栏里消失
    for (const QString &k : all)
        if (!order.contains(k))
            order << k;
    m_navOrder = order;

    static const QStringList pages = {
        QStringLiteral("home"),      QStringLiteral("drive"),
        QStringLiteral("photos"),    QStringLiteral("passwords"),
        QStringLiteral("bookmarks"), QStringLiteral("settings")
    };
    const QString dp =
        s.value(QStringLiteral("nav/defaultPage")).toString();
    m_defaultPage = pages.contains(dp) ? dp : QStringLiteral("home");
}

void Bridge::saveNavOrder()
{
    QSettings s = paths::userSettings();
    s.setValue(QStringLiteral("nav/order"), m_navOrder);
    s.sync();
}

void Bridge::loadUiSettings()
{
    QSettings s = paths::userSettings();
    // 默认 false = 点关闭按钮直接退出, 与历史行为一致(不改变老用户习惯)。
    m_closeToTray =
        s.value(QStringLiteral("ui/closeToTray"), false).toBool();
}

void Bridge::setCloseToTray(bool on)
{
    if (m_closeToTray == on)
        return;
    m_closeToTray = on;

    QSettings s = paths::userSettings();
    s.setValue(QStringLiteral("ui/closeToTray"), on);
    s.sync();

    log::userAction(on ? QStringLiteral("关闭按钮 -> 关闭窗口但进程后台常驻")
                       : QStringLiteral("关闭按钮 -> 关闭即退出程序"));
    emit settingsChanged();
}

void Bridge::moveCategory(const QString &key, int delta)
{
    const int i = m_navOrder.indexOf(key);
    if (i < 0)
        return;
    const int j = i + delta;
    // 不允许越过边界 —— 首页和设置在导航里是固定的, 不在这张表里,
    // 所以这里的边界就是中间四类自己的首尾。
    if (j < 0 || j >= m_navOrder.size())
        return;

    m_navOrder.move(i, j);
    saveNavOrder();
    log::userAction(QStringLiteral("%1 移到第 %2 位")
                        .arg(categoryName(key)).arg(j + 1));
    emit navOrderChanged();
}

void Bridge::moveCategoryTo(const QString &key, int toIndex)
{
    const int from = m_navOrder.indexOf(key);
    if (from < 0)
        return;
    if (toIndex < 0 || toIndex >= m_navOrder.size())
        return;
    if (from == toIndex)
        return;

    // ★ 注意 m_navOrder 只含**中间四类**(首页/设置是固定的, 不在里面),
    //   所以这里的下标就是四类自己的序号, 与设置页列表的行号一致。
    //   拖拽时拿 ListView 的目标行号直接传进来即可, 边界判断在这里做。
    m_navOrder.move(from, toIndex);
    saveNavOrder();
    log::userAction(QStringLiteral("%1 从第 %2 位拖到第 %3 位")
                        .arg(categoryName(key))
                        .arg(from + 1).arg(toIndex + 1));
    emit navOrderChanged();
}

void Bridge::resetNavOrder()
{
    m_navOrder = categoryKeys();
    saveNavOrder();
    log::userAction(QStringLiteral("侧边栏顺序已恢复默认"));
    emit navOrderChanged();
}

void Bridge::setDefaultPage(const QString &p)
{
    static const QStringList pages = {
        QStringLiteral("home"),      QStringLiteral("drive"),
        QStringLiteral("photos"),    QStringLiteral("passwords"),
        QStringLiteral("bookmarks"), QStringLiteral("settings")
    };
    if (!pages.contains(p))
        return;
    m_defaultPage = p;
    QSettings s = paths::userSettings();
    s.setValue(QStringLiteral("nav/defaultPage"), p);
    s.sync();
    log::userAction(QStringLiteral("默认启动页 -> %1").arg(p));
    emit settingsChanged();
}

void Bridge::onAnomalyCleared()
{
    m_anomalous = false;
    m_anomalyTitle.clear();
    m_anomalyDetail.clear();
    m_anomalyList.clear();
    emit anomalyChanged();
    emit notify(QStringLiteral("ok"), QStringLiteral("同步已恢复正常"));
}

// ── getter 实现 ───────────────────────────────────────────────────
QString Bridge::stateName() const   { return m_snap.stateName(); }
QString Bridge::stateDetail() const { return m_snap.stateDetail(); }
bool    Bridge::busy() const        { return m_snap.isBusy(); }
bool    Bridge::inSync() const      { return m_snap.inSync; }
bool    Bridge::dbOk() const        { return m_snap.dbOk; }
QString Bridge::clientVersion() const { return m_snap.clientVersion; }
QString Bridge::timestamp() const
{
    return QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
}

int Bridge::upDone() const  { return m_snap.queue.syncedUpCount; }
int Bridge::upTotal() const
{
    return m_snap.queue.syncedUpCount + m_snap.queue.needUpCount;
}
QString Bridge::upSize() const
{
    return PendingModel::humanSize(m_snap.queue.needUpBytes);
}

int Bridge::dlDone() const  { return m_snap.queue.downloadedCount; }
int Bridge::dlTotal() const
{
    return m_snap.queue.downloadedCount + m_snap.queue.needDlCount;
}
QString Bridge::dlSize() const
{
    return PendingModel::humanSize(m_snap.queue.needDlBytes);
}
bool Bridge::dlBusy() const
{
    return m_snap.queue.needDlCount > 0 || m_snap.queue.pendingFetches > 0;
}

QString Bridge::lastError() const
{
    if (!m_snap.dbOk)
        return m_snap.dbError;
    if (m_snap.faults > 0)
        return QStringLiteral("故障项 %1 个").arg(m_snap.faults);
    if (!m_snap.uploadErrors.isEmpty()) {
        QStringList l;
        for (const auto &p : m_snap.uploadErrors)
            l << QStringLiteral("码%1 x%2").arg(p.first).arg(p.second);
        return QStringLiteral("上传错误 ") + l.join(QStringLiteral(", "));
    }
    if (!m_snap.downloadErrors.isEmpty()) {
        QStringList l;
        for (const auto &p : m_snap.downloadErrors)
            l << QStringLiteral("码%1 x%2").arg(p.first).arg(p.second);
        return QStringLiteral("下载错误 ") + l.join(QStringLiteral(", "));
    }
    return QString();
}

QString Bridge::rankText() const
{
    if (!m_snap.hasRank)
        return QStringLiteral("游标不可读");
    return QStringLiteral("client=%1  server=%2  待应用=%3  -> %4")
        .arg(m_snap.rankClient)
        .arg(m_snap.rankServer)
        .arg(m_snap.unapplied)
        .arg(m_snap.inSync ? QStringLiteral("已追平")
                           : QStringLiteral("追赶中"));
}

// ══════════════════════════════════════════════════════════════════
//  iCloud 存储配额
// ══════════════════════════════════════════════════════════════════

// 下面的 getter 一律读缓存 m_quota —— 它由 refreshQuota() 在每次采样时
// 重新从偏好文件读取并填好(数值变化时才 emit quotaChanged)。
// 所以既不会显示"启动时的旧快照", 也不会在渲染时被反复读盘。

//bool Bridge::quotaOk() const      { return m_quota.ok; }
//QString Bridge::quotaError() const { return m_quota.error; }

//static QString humanGB(qint64 bytes)
//{
//    if (bytes <= 0)
//        return QStringLiteral("0 GB");
//    const double gb = double(bytes) / (1024.0 * 1024.0 * 1024.0);
//    // 跟 Apple 界面一致: 小于 10GB 显示一位小数(如 "9.4 GB"),
//    // 更大则取整(如 "32 GB"), 免得出现 "1234.5 GB" 这种长数字。
//    if (gb < 10.0)
//        return QStringLiteral("%1 GB").arg(gb, 0, 'f', 1);
//    return QStringLiteral("%1 GB").arg(qRound(gb));
//}

//QString Bridge::quotaTotalText() const
//{
//    const auto &q = m_quota;
//    return q.ok ? humanGB(q.totalQuota) : QString();
//}

//QString Bridge::quotaUsedText() const
//{
//    const auto &q = m_quota;
//    return q.ok ? humanGB(q.totalUsed) : QString();
//}

//QString Bridge::quotaFreeText() const
//{
//    const auto &q = m_quota;
//    return q.ok ? humanGB(q.totalFree) : QString();
//}

//double Bridge::quotaUsedRatio() const
//{
//    const auto &q = m_quota;
//    if (!q.ok || q.totalQuota <= 0)
//        return 0.0;
//    return double(q.totalUsed) / double(q.totalQuota);
//}

//bool Bridge::quotaOver() const { return m_quota.overQuota; }

//QVariantList Bridge::quotaBuckets() const
//{
//    QVariantList out;
//    const auto &q = m_quota;
//    if (!q.ok || q.totalQuota <= 0)
//        return out;

//    for (const icloud::QuotaBucket &b : q.buckets) {
//        QVariantMap m;
//        m.insert(QStringLiteral("label"), b.label);
//        m.insert(QStringLiteral("bytes"), double(b.bytes));
//        m.insert(QStringLiteral("bytesText"), humanGB(b.bytes));
//        m.insert(QStringLiteral("color"), b.colorHex);
//        // 占整条的比例 —— 各段之和正好等于 quotaUsedRatio。
//        // 归一到 totalQuota 而不是 used, 这样"已用条"的长度就等于
//        // 已用占比, 与左侧的总量/已用数字对得上。
//        m.insert(QStringLiteral("ratio"), double(b.bytes) / double(q.totalQuota));
//        out.append(m);
//    }
//    return out;
//}

//void Bridge::openQuotaManage()
//{
//    const auto &q = m_quota;
//    if (q.manageUrl.isEmpty()) {
//        emit notify(QStringLiteral("info"),
//                    QStringLiteral("拿不到 Apple 的储存空间管理地址"));
//        return;
//    }
//    log::userAction(QStringLiteral("打开 iCloud 储存空间管理页"));
//    QDesktopServices::openUrl(QUrl(q.manageUrl));
//}

QVariantList Bridge::pcsBuckets() const
{
    QVariantList out;
    out.reserve(m_snap.pcsDist.size());
    for (const PcsBucket &b : m_snap.pcsDist) {
        QVariantMap m;
        m.insert(QStringLiteral("pcsState"), b.pcsState);
        m.insert(QStringLiteral("count"),    b.count);
        m.insert(QStringLiteral("bytes"),    double(b.bytes));
        m.insert(QStringLiteral("label"),    b.label);
        out.append(m);
    }
    return out;
}

void Bridge::setRefreshSec(int s)
{
    if (s < 1) s = 1;
    if (s > 3600) s = 3600;
    m_refreshSec = s;
    log::userAction(QStringLiteral("刷新间隔 -> %1 秒").arg(s));
    m_monitor->start(s * 1000);
    emit settingsChanged();
}

void Bridge::setStallSec(int s)
{
    if (s < 10) s = 10;
    if (s > 86400) s = 86400;
    m_stallSec = s;
    log::userAction(QStringLiteral("卡死阈值 -> %1 秒").arg(s));
    m_monitor->setStallSeconds(s);
    emit settingsChanged();
}

}  // namespace icloud
