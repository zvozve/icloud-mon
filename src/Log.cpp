#include "Log.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QString>

#include "spdlog/sinks/daily_file_sink.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"

#include <cstdint>

namespace icloud {
namespace log {

namespace {

bool g_inited = false;
QString g_dir;

// 同一天内日志超过这个大小就额外切一个序号文件, 防止单文件无限膨胀。
// 一天正常采样(5s 一次)约 1~2MB, 20MB 足够宽松。
constexpr std::size_t kMaxSizePerDay = 20 * 1024 * 1024;
// 保留最近 30 天。注意类型: daily_file_sink 的 max_files 是 uint16_t,
// 用 int 传会触发窄化转换。
constexpr std::uint16_t kMaxFiles = 30;

}  // namespace

bool init()
{
    if (g_inited)
        return true;

    // 日志目录: exe 同级\log\。用 applicationDirPath 而非 AppData,
    // 因为这个目录要能直接拷给排障的人。
    const QString exeDir = QCoreApplication::applicationDirPath();
    g_dir = exeDir + QDir::separator() + QStringLiteral("log");

    if (!QDir().mkpath(g_dir)) {
        // 建不了目录就别硬写, 否则每条日志都报错刷屏
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

        // 按天切分(23:59 换日), 保留最近 30 天, 单日超 20MB 再切
        // 签名: (文件名, 小时, 分钟, max_files, rotation_size, handlers)
        // 最后那个是 file_event_handlers, 不是 bool —— 传错会编译失败。
        auto file = std::make_shared<spdlog::sinks::daily_file_sink_mt>(
            base, 23, 59, kMaxFiles, kMaxSizePerDay);
        file->set_level(spdlog::level::trace);

        // third arg = 自动 flush(级别)
        auto logger = std::make_shared<spdlog::logger>(
            "icloudmon", spdlog::sinks_init_list{console, file});
        logger->set_level(spdlog::level::trace);
        logger->flush_on(spdlog::level::warn);

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

void shutdown()
{
    if (!g_inited)
        return;
    // 注意: 没有 spdlog::flush() 这个自由函数, 只有
    // spdlog::default_logger()->flush() 与 spdlog::flush_on(level)。
    if (auto lg = spdlog::default_logger())
        lg->flush();
    spdlog::shutdown();
    g_inited = false;
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
    spdlog::info("[SYNC] {} -> {} | {}",
                 utf8(from), utf8(to), utf8(detail));
}

void userAction(const QString &action, const QString &result)
{
    if (result.isEmpty())
        spdlog::info("[ACTION] {}", utf8(action));
    else
        spdlog::info("[ACTION] {} -> {}", utf8(action), utf8(result));
}

}  // namespace log
}  // namespace icloud