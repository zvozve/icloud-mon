#include "Log.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QString>

#include "spdlog/sinks/daily_file_sink.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"

#include <cstdint>
#include <atomic>

namespace icloud {
namespace log {

namespace {

// 日志通道是否打开。跨线程读写(SystemNotifier 的 STA 线程也写日志,
// 而 init/shutdown 在 UI 线程), 所以用 atomic, 不做无锁假设。
std::atomic<bool> g_inited{false};
QString g_dir;

// 保留最近 30 天。注意类型: daily_file_sink 的 max_files 是 uint16_t,
// 用 int 传会触发窄化转换。
constexpr std::uint16_t kMaxFiles = 30;

}  // namespace

bool init()
{
    if (g_inited)
        return true;

    // 日志目录策略: 优先 exe 同级\log\ (开发/绿色版时方便直接拷给排障的人),
    // 但**必须探测真实可写**再决定用哪个:
    //   · 装到 Program Files 是只读的 -> 那里建目录/写文件会失败;
    //   · Enigma VB 打包的单文件是**虚拟化**运行, exe 目录在容器里,
    //     mkpath 可能"成功"但写进去的日志随容器销毁而丢失, 等于没记。
    // 所以依次尝试 exe同级 与 用户AppData, 用"能不能真的写出一个探针文件"
    // 来判定, 第一个可写的就用它; 都不可写就只留 stderr(不硬写)。
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        exeDir + QDir::separator() + QStringLiteral("log"),
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + QDir::separator() + QStringLiteral("log"),
    };
    g_dir.clear();
    for (const QString &c : candidates) {
        if (c.isEmpty() || !QDir().mkpath(c))
            continue;
        // 真实写一个探针文件验证可写(虚拟化目录 mkpath 可能假成功)
        const QString probe = c + QDir::separator() + QStringLiteral(".wtest");
        QFile f(probe);
        if (f.open(QIODevice::WriteOnly)) {
            f.close();
            QFile::remove(probe);
            g_dir = c;
            break;
        }
    }
    if (g_dir.isEmpty()) {
        // 一个都写不了: 别硬写, 否则每条日志都报错刷屏
        g_inited = true;
        return false;
    }

    // ASCII 路径最稳: spdlog 内部按窄字符处理, 中文目录名在部分
    // Windows 环境下会被转成乱码导致文件创建失败。
    const QString stem = QDir::toNativeSeparators(
        g_dir + QDir::separator() + QStringLiteral("icloudmon"));
    const std::string base = stem.toStdString();

    try {
        // console sink: 开发期直接看 stderr, 排错快
        auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console->set_level(spdlog::level::warn);

        // 按天切分(23:59 换日), 保留最近 30 天。
        // ★ spdlog 1.14 的真实签名是
        //   (文件名, 小时, 分钟, truncate, max_files, file_event_handlers)
        //   —— 第 4 个参数是 **bool truncate**, 不是 max_files。
        //   老代码按 (..., max_files, rotation_size) 的顺序传, 于是:
        //     truncate  = 30        -> true, 每次启动把当天日志清空;
        //     max_files = 20971520  -> 窄化成 uint16_t 后为 0, 30 天保留失效。
        //   即注释声称的两件事一个都没做到, 反而丢掉重启前的诊断信息。
        //   现按真实签名传, 并把无法实现的"按大小切"说法去掉 ——
        //   daily_file_sink 只能按时间切, 没有按大小切的能力。
        auto file = std::make_shared<spdlog::sinks::daily_file_sink_mt>(
            base, 23, 59, /*truncate=*/false, kMaxFiles);
        file->set_level(spdlog::level::trace);

        // third arg = 自动 flush(级别)
        auto logger = std::make_shared<spdlog::logger>(
            "icloudmon", spdlog::sinks_init_list{console, file});
        logger->set_level(spdlog::level::trace);
        // 每条 info 及以上立即落盘。
        // 原来只 flush_on(warn), 结果 info 级记录全留在缓冲区里 ——
        // 进程被强杀(任务管理器结束进程 / SIGTERM / 崩溃)时全部丢失,
        // 而这恰恰是最需要看日志的时候。
        // 本应用日志量很小(状态变化才写一行), 逐条 flush 的代价可忽略。
        logger->flush_on(spdlog::level::info);

        spdlog::set_default_logger(std::move(logger));

        g_inited = true;
        return true;
    } catch (const spdlog::spdlog_ex &ex) {
        // 初始化失败时不能递归调 error() —— 默认 logger 还没建好,
        // 会无限递归。静默降级为"不写文件"。
        (void)ex;
        g_inited = true;
        return false;
    }
}

bool active()
{
    return g_inited.load(std::memory_order_relaxed);
}

void shutdown()
{
    if (!g_inited)
        return;
    // 注意: 没有 spdlog::flush() 这个自由函数, 只有
    // spdlog::default_logger()->flush() 与 spdlog::flush_on(level)。
    if (auto lg = spdlog::default_logger())
        lg->flush();
    // ★ 先置闸再销毁: spdlog::shutdown() 之后 default logger 就没了,
    //   而调用方(main)在这之后还要析构一大堆会写日志的对象 ——
    //   闸门不先落下, 那些日志就是往空指针上写。
    g_inited = false;
    spdlog::shutdown();
}

QString logDir()
{
    return g_dir;
}

QString currentLogFile()
{
    // 直接问 sink 要当前文件名, 不自己拼 —— 拼错日期格式会给出
    // 一个根本不存在的路径, 比不给更糟。
    try {
        auto lg = spdlog::get("icloudmon");
        for (auto &sink : lg->sinks()) {
            if (auto *daily = dynamic_cast<spdlog::sinks::daily_file_sink_mt *>(
                    sink.get())) {
                return QString::fromStdString(daily->filename());
            }
        }
    } catch (const spdlog::spdlog_ex &) {
        // logger 不可用时退回目录路径
    }
    return g_dir;
}

void syncChanged(const QString &from, const QString &to,
                 const QString &detail)
{
    logAt(spdlog::level::info,
          utf8(QStringLiteral("[SYNC] %1 -> %2 | %3").arg(from, to, detail)));
}

void userAction(const QString &action, const QString &result)
{
    if (result.isEmpty())
        logAt(spdlog::level::info,
              utf8(QStringLiteral("[ACTION] %1").arg(action)));
    else
        logAt(spdlog::level::info,
              utf8(QStringLiteral("[ACTION] %1 -> %2").arg(action, result)));
}

}  // namespace log
}  // namespace icloud