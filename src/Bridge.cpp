#include "Bridge.h"

#include <QtMath>

#include <QDateTime>
#include <QElapsedTimer>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
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

// ★ 过渡桥(临时, 有明确删除条件) ──────────────────────────────────
// 四类同步分类(IcloudCategories)还没迁到 Python —— 见 doc/待办/01 的
// "四类同步分类移植"。它按 Snapshot 取值, 所以这里把 payload 里的**原始计数**
// 字段反填成最小 Snapshot。只填 readDrive() 真正用到的字段; stateName()/
// stateDetail() 是派生方法, 填好原值后自会算出与 Python 相同的结果。
// 分类迁移完成后, 本函数与 IcloudCategories 一起删。
Snapshot snapshotForCategories(const QJsonObject &p)
{
    Snapshot s;
    s.dbOk          = p.value(QStringLiteral("dbOk")).toBool();
    s.clientVersion = p.value(QStringLiteral("clientVersion")).toString();
    s.hasRank       = p.value(QStringLiteral("hasRank")).toBool();
    s.rankClient    = qint64(p.value(QStringLiteral("rankClient")).toDouble());
    s.rankServer    = qint64(p.value(QStringLiteral("rankServer")).toDouble());
    s.unapplied     = p.value(QStringLiteral("unapplied")).toInt();
    s.inSync        = p.value(QStringLiteral("inSync")).toBool();
    s.faults        = p.value(QStringLiteral("faults")).toInt();
    s.overQuota     = p.value(QStringLiteral("overQuota")).toInt();
    s.cloudOnlyCount = p.value(QStringLiteral("cloudOnlyCount")).toInt();
    s.trashCount    = p.value(QStringLiteral("trashCount")).toInt();

    QueueStat &q = s.queue;
    q.needUpCount   = p.value(QStringLiteral("needUpCount")).toInt();
    q.needUpBytes   = qint64(p.value(QStringLiteral("needUpBytes")).toDouble());
    q.needDlCount   = p.value(QStringLiteral("needDlCount")).toInt();
    q.needDlBytes   = qint64(p.value(QStringLiteral("needDlBytes")).toDouble());
    q.needDelCount  = p.value(QStringLiteral("needDelCount")).toInt();
    q.pendingFetches = p.value(QStringLiteral("pendingFetches")).toInt();
    q.dlUnstaged    = p.value(QStringLiteral("dlUnstaged")).toInt();

    // 本地未提交项: 分类页只关心"有没有", 给一条占位即可让 isBusy/stateName
    // 的判据与旁路一致(localPendings 非空 => 可能是"待提交")。
    if (p.value(QStringLiteral("localPendingTotal")).toInt() > 0) {
        LocalPending lp;
        lp.name = QStringLiteral("(见首页待处理面板)");
        s.localPendings.append(lp);
        s.localPendingTotal = p.value(QStringLiteral("localPendingTotal")).toInt();
    }
    return s;
}

}  // namespace

Bridge::Bridge(QObject *parent)
    : QObject(parent)
    , m_sidecar(new SidecarClient(this))
    , m_service(new IcloudService(this))
    , m_autoStart(new AutoStart(this))
{
    // ── 数据源 = Python 旁路 ──
    // IcloudMonitor(C++ 直读 db) 已不再被引用(见 CMakeLists 与 IcloudMonitor.h
    // 顶部的 deprecated 说明)。取数与判定只在 Python 侧实现一次。
    connect(m_sidecar, &SidecarClient::snapshotReady,
            this, &Bridge::onSidecarSnapshot);
    connect(m_sidecar, &SidecarClient::onlineChanged,
            this, &Bridge::onSidecarOnlineChanged);

    // ── 客户端控制 = IcloudService(C++) ──
    // 启停留在 C++: 与 Python 侧效果打平, 但已实现且实测过坑, 还能直接驱动
    // 界面上的 busy/progress 文案, 省一次 IPC 往返。见 doc/架构边界与风险控制.md
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
    // 采样间隔在旁路侧生效(stall 阈值同理由旁路持有, 见 sidecar/stall.py)
    m_sidecar->start();
    m_sidecar->setRefreshSec(m_refreshSec);
}

void Bridge::stopMonitoring()
{
    m_sidecar->stopSampling();
}

void Bridge::refreshNow()
{
    m_sidecar->refreshNow();
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
    m_sidecar->refreshNow();   // 重启后同步状态会变, 立刻采一次
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
// 基线文件由旁路维护(sidecar/stall.py), 位置与旧 C++ 版一致:
// %APPDATA%\iCloudMon\iCloudMon\icloud_state.jsonl
void Bridge::resetHistory()
{
    m_sidecar->resetHistory();
    log::userAction(QStringLiteral("清空卡死检测基线"), QStringLiteral("成功"));
    emit notify(QStringLiteral("ok"), QStringLiteral("已清空历史基线"));
}

QString Bridge::stateFilePath() const
{
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return dir + QDir::separator() + paths::stateFile();
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
//
// 值的真源在旁路: sidecar/icloudmon/snapshot.py 的 current_drive_root()
// 按 env > 注册表(用户覆盖) > 数据库推导 > %USERPROFILE%\iCloudDrive 解析,
// 结果随 payload 的 "driveRoot" 下发(**数据库推导只在 Python 侧做, 因为
// 只有它读得到 iCloudDrive.db**)。
// C++ 这层不再自己算根目录:
//   · 有 payload 值  -> 用它;
//   · 旁路离线       -> 用最后一次收到的 m_driveRoot(配置不是同步数据);
//   · 从未收到过     -> 退回 paths::driveRoot()(env>注册表>标准安装位置)。
QString Bridge::driveRoot() const
{
    const QString fromSidecar = jstr(m_payload, "driveRoot");
    if (!fromSidecar.isEmpty())
        return fromSidecar;
    if (!m_driveRoot.isEmpty())
        return m_driveRoot;
    return paths::driveRoot();
}

bool Bridge::driveRootIsCustom() const
{
    return paths::driveRootIsCustom();
}

void Bridge::openDriveRoot()
{
    const QString dir = driveRoot();
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
    const QString dir = driveRoot() + QStringLiteral("/.Trash");
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
    // ★ 这里原来用「iCloud Drive 根目录里必然有 .icloud 隐藏文件」做二次
    //   校验 —— **实测是错的**: 根目录下根本没有 .icloud(`.icloud` 是
    //   **单个文件**被驱逐后留下的占位符, 不是根标记)。那句永远是
    //   "未检测到", 写了等于没写, 还会误导排障。
    //   现在改用根目录的**权威特征**: iCloud 在同步根下必建 .Trash 回收站
    //   (与 Python 侧反推根目录用的是同一个判据)。仅作提示, 不阻止用户 ——
    //   目录刚建好、还没同步过东西时 .Trash 可能尚未出现。
    const bool looksLikeDrive = QDir(clean + QStringLiteral("/.Trash")).exists();

    QString err;
    if (!paths::setDriveRoot(clean, &err)) {
        log::userAction(QStringLiteral("设置同步根目录=%1").arg(clean),
                        QStringLiteral("失败: %1").arg(err));
        emit notify(QStringLiteral("error"),
                    QStringLiteral("保存失败: %1").arg(err));
        return false;
    }

    log::userAction(QStringLiteral("设置同步根目录=%1").arg(clean),
                    looksLikeDrive ? QStringLiteral("成功(检测到 .Trash)")
                                   : QStringLiteral("成功(未检测到 .Trash, 仅提示)"));
    emit driveRootChanged();
    emit notify(QStringLiteral("ok"), QStringLiteral("同步根目录已更新"));
    // 路径变了, 立刻重采一次让界面跟上(旁路每帧都从注册表读根目录)
    m_sidecar->refreshNow();
    return true;
}

void Bridge::resetDriveRoot()
{
    paths::clearDriveRoot();
    log::userAction(QStringLiteral("恢复默认同步根目录"), driveRoot());
    emit driveRootChanged();
    emit notify(QStringLiteral("ok"), QStringLiteral("已恢复默认路径"));
    // 清掉覆盖值后立刻重采: 界面上的路径要刷新成"推导/标准位置"的结果
    m_sidecar->refreshNow();
}

// ── 快照处理(来自 Python 旁路的 NDJSON) ───────────────────────────
//
// ★ 单一真相源: 这里**不做任何判定**, 只把 payload 落到模型与缓存。
//   状态名/详情/忙闲/异常全部由旁路给出(sidecar/bridge.py), C++ 侧再算一遍
//   就等于有两个真相源 —— 这是本文件历史上最容易变乱的地方, 别退回去。
void Bridge::onSidecarSnapshot(const QJsonObject &payload)
{
    m_payload = payload;

    // 同步根目录由旁路解析后下发(env>注册表>数据库推导>标准位置)。
    // 单独缓存: 它属于"配置", 离线时不清空(否则界面会显示错误路径)。
    const QString root = jstr(payload, "driveRoot");
    if (!root.isEmpty() && root != m_driveRoot) {
        m_driveRoot = root;
        emit driveRootChanged();
    }

    // 明细列表 -> 三个 ListModel(角色名不变, QML 绑定无需改动)
    QVector<PendingItem> pend;
    for (const QJsonValue &v : payload.value(QStringLiteral("pendingItems")).toArray()) {
        const QJsonObject o = v.toObject();
        PendingItem it;
        it.name    = o.value(QStringLiteral("name")).toString();
        it.bytes   = qint64(o.value(QStringLiteral("size")).toDouble());
        it.isDir   = o.value(QStringLiteral("isDir")).toBool();
        it.path    = o.value(QStringLiteral("path")).toString();
        it.missing = o.value(QStringLiteral("missing")).toBool();
        pend.append(it);
    }
    m_pendingModel.setItems(pend);

    QVector<LocalPending> loc;
    for (const QJsonValue &v : payload.value(QStringLiteral("localPendings")).toArray()) {
        const QJsonObject o = v.toObject();
        LocalPending p;
        p.name    = o.value(QStringLiteral("name")).toString();
        p.bytes   = qint64(o.value(QStringLiteral("bytes")).toDouble());
        p.absPath = o.value(QStringLiteral("path")).toString();
        // stateText 仅用于展示, 这里按字符串是否存在判断标志位
        const QString st = o.value(QStringLiteral("stateText")).toString();
        p.inFlight = st.contains(QStringLiteral("inflight"));
        p.hasDiff  = st.contains(QStringLiteral("diff"));
        loc.append(p);
    }
    m_localModel.setItems(loc);

    QVector<TrashItem> trash;
    for (const QJsonValue &v : payload.value(QStringLiteral("trashItems")).toArray()) {
        const QJsonObject o = v.toObject();
        TrashItem t;
        t.name        = o.value(QStringLiteral("name")).toString();
        t.restorePath = o.value(QStringLiteral("restorePath")).toString();
        trash.append(t);
    }
    m_trashModel.setItems(trash);

    applyAnomalies(payload.value(QStringLiteral("anomalies")).toArray());

    refreshCategories();
    emit snapshotChanged();
}

// 异常落到 m_anomaly*: 语义与旧 C++ 版逐条一致(见原 Bridge::onAnomaly 注释)
void Bridge::applyAnomalies(const QJsonArray &arr)
{
    const bool wasAnomalous = m_anomalous;
    const bool nowAnomalous = !arr.isEmpty();

    m_anomalyList.clear();
    m_anomalyTitle.clear();
    m_anomalyDetail.clear();

    bool isNewKind = false;
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        const QString kind   = o.value(QStringLiteral("kind")).toString();
        const QString title  = o.value(QStringLiteral("title")).toString();
        const QString detail = o.value(QStringLiteral("detail")).toString();
        // 与旧实现一致: 后一条覆盖标题/详情
        m_anomalyTitle  = title;
        m_anomalyDetail = detail;
        if (!m_anomalyList.contains(kind)) {
            m_anomalyList.append(kind);
            isNewKind = true;
        }
    }
    m_anomalous = nowAnomalous;

    // 弹窗只在新出现 / 换了种类时给。持续卡死状态下每帧都弹的话,
    // Toast 动画会被反复重启, 既是持续打扰也让界面反复重绘。
    if (nowAnomalous && (!wasAnomalous || isNewKind)) {
        const QString kind = m_anomalyList.value(0);
        // 卡死 -> 置顶浮层系统通知(跨应用可见); 其余 -> 窗口内 Toast
        if (m_anomalyList.contains(QStringLiteral("卡死"))) {
            log::userAction(QStringLiteral("卡死告警触发系统通知"),
                            QStringLiteral("停滞 %1").arg(m_anomalyDetail));
            emit systemNotify(m_anomalyTitle, m_anomalyDetail);
        } else {
            emit notify(QStringLiteral("warn"),
                        QStringLiteral("[%1] %2").arg(kind, m_anomalyTitle));
        }
    } else if (!nowAnomalous && wasAnomalous) {
        log::info(QStringLiteral("[ANOMALY] 已恢复, 同步回到正常"));
        emit notify(QStringLiteral("ok"), QStringLiteral("同步已恢复正常"));
    }
    emit anomalyChanged();
}

// 旁路离线: 明确不可用, **不沿用旧数据**
void Bridge::applyOffline(const QString &reason)
{
    m_payload = QJsonObject();      // 清空 -> getter 全部回落为 0/"—"
    m_pendingModel.setItems({});
    m_localModel.setItems({});
    m_trashModel.setItems({});
    // 合成一帧"不可用"快照: 状态区直接显示原因, 数字归零
    QJsonObject p;
    p.insert(QStringLiteral("dbOk"), false);
    p.insert(QStringLiteral("stateName"), QStringLiteral("监控不可用"));
    p.insert(QStringLiteral("stateDetail"),
             reason.isEmpty() ? QStringLiteral("监控旁路未运行") : reason);
    p.insert(QStringLiteral("busy"), false);
    m_payload = p;
    emit snapshotChanged();
}

void Bridge::onSidecarOnlineChanged(bool online, const QString &reason)
{
    m_sidecarOnline = online;
    m_sidecarOfflineReason = online ? QString() : reason;

    if (online) {
        log::info(QStringLiteral("[BRIDGE] 旁路在线, 开始接收采样"));
        emit notify(QStringLiteral("ok"), QStringLiteral("监控已恢复"));
    } else {
        // ★ 标签用 [BRIDGE] 而不是 [SIDECAR]:
        //   同一件事 SidecarClient::goOffline 已经记过一条了。两处都打
        //   "[SIDECAR] 离线: <同一句话>" 时, 日志里会出现两条时间戳相同、
        //   文案相同的行, 看上去像重复调用 —— 排障时白查一轮。
        //   这里记的是"界面侧已切到不可用", 用不同标签区分开。
        log::error(QStringLiteral("[BRIDGE] 旁路离线, 界面已切到「监控不可用」: %1")
                       .arg(reason));
        applyOffline(reason);
        emit notify(QStringLiteral("warn"),
                    QStringLiteral("监控不可用: %1").arg(reason));
    }
    emit sidecarStateChanged();
}

    // ★ 当前**未启用**: 自研的 BPlist 解析器还没调通(Apple 这个偏好文件
    //   用的不是标准 bplist 编码: 整数是 0x1_、bool 是 0x08/0x09)。
    //   代码保留在 IcloudQuota.cpp / BPlist.cpp, 等解析器验证通过后再
    //   挂回这里并接上首页那张存储卡片。
    //   现在不调用 = 不会每 3 秒读文件、不会打无意义的解析失败警告。

// 说明: 旧的 onAnomaly / onAnomalyCleared 已删除 —— 异常判定迁到旁路,
// 由 applyAnomalies() 统一处理(上面)。不留空壳, 避免出现"两个入口做同一件事"。

void Bridge::testSystemNotification()
{
    log::userAction(QStringLiteral("测试提醒"));
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
    // 四类分类尚未迁到 Python, 见 snapshotForCategories() 的说明
    m_categories = IcloudCategories::readAll(snapshotForCategories(m_payload));
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

    log::userAction(on ? QStringLiteral("退出按钮 -> 关闭窗口但进程后台常驻")
                       : QStringLiteral("退出按钮 -> 直接结束程序"));
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

// ── getter 实现 ───────────────────────────────────────────────────
//
// 全部直接读最近一帧 payload(旁路的 JSON)。旁路离线时 payload 被替换成
// 一帧"监控不可用"(见 applyOffline), 所以这里不会出现"离线还显示旧数字"。
// 取值小工具 jint/jbool/jstr 定义在 Bridge.h(类内 inline getter 也要用)。

QString Bridge::stateName() const
{
    if (!m_payload.contains(QStringLiteral("stateName")))
        return QStringLiteral("未知");
    return jstr(m_payload, "stateName");
}

QString Bridge::stateDetail() const
{
    if (!m_payload.contains(QStringLiteral("stateDetail")))
        return QStringLiteral("正在启动监控…");
    return jstr(m_payload, "stateDetail");
}

bool Bridge::busy() const        { return jbool(m_payload, "busy"); }
bool Bridge::inSync() const      { return jbool(m_payload, "inSync"); }
bool Bridge::dbOk() const        { return jbool(m_payload, "dbOk"); }
QString Bridge::clientVersion() const { return jstr(m_payload, "clientVersion"); }
QString Bridge::timestamp() const
{
    const QString ts = jstr(m_payload, "timestamp");
    return ts.isEmpty()
        ? QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"))
        : ts;
}

int Bridge::upDone() const  { return jint(m_payload, "upDone"); }
int Bridge::upTotal() const { return jint(m_payload, "upTotal"); }
QString Bridge::upSize() const { return jstr(m_payload, "upSize"); }

int Bridge::dlDone() const  { return jint(m_payload, "dlDone"); }
int Bridge::dlTotal() const { return jint(m_payload, "dlTotal"); }
QString Bridge::dlSize() const { return jstr(m_payload, "dlSize"); }
bool Bridge::dlBusy() const { return jbool(m_payload, "dlBusy"); }

QString Bridge::lastError() const
{
    return jstr(m_payload, "lastError");
}

QString Bridge::rankText() const
{
    const QString t = jstr(m_payload, "rankText");
    return t.isEmpty() ? QStringLiteral("游标不可读") : t;
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
    const QJsonArray arr = m_payload.value(QStringLiteral("pcsBuckets")).toArray();
    out.reserve(arr.size());
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        QVariantMap m;
        m.insert(QStringLiteral("pcsState"), o.value(QStringLiteral("pcsState")).toInt());
        m.insert(QStringLiteral("count"),    o.value(QStringLiteral("count")).toInt());
        m.insert(QStringLiteral("bytes"),    o.value(QStringLiteral("bytes")).toDouble());
        m.insert(QStringLiteral("label"),    o.value(QStringLiteral("label")).toString());
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
    m_sidecar->setRefreshSec(s);
    emit settingsChanged();
}

void Bridge::setStallSec(int s)
{
    if (s < 10) s = 10;
    if (s > 86400) s = 86400;
    m_stallSec = s;
    log::userAction(QStringLiteral("卡死阈值 -> %1 秒").arg(s));
    // 判定在旁路侧, 这里仅记录用于展示; 阈值随下一帧采样由旁路读取
    // (当前实现: 旁路用自己的默认值, 见 doc/待办/01 的"阈值下发"待办)
    m_sidecar->setSetting(QStringLiteral("ui/stallSec"), s);
    emit settingsChanged();
}

}  // namespace icloud
