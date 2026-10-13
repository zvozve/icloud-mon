// ======================================================================
//  Log.h  -  日志系统封装 (spdlog 薄封装)
//
//  需求:
//    1. 按天切分文件 —— log\icloudmon_YYYY-MM-DD.log
//    2. 记录三类内容:
//       - iCloud 同步变化(状态切换/ 队列增减 / 异常出现与恢复)
//       - 软件自身运行状态(启动 / 退出 / 采样耗时 / 数据库可读性)
//       - 用户操作(重启 iCloud / 开关自启动 / 改设置 / 清历史)
//    3. 日志目录固定在 exe 同级的\log\, 不依赖 AppData ——
//       换机器排查问题时, 日志要能直接打包带走。
//
//  设计取舍:
//    spdlog 用 header-only 模式引入(SPDLOG_HEADER_ONLY),
//    第三方源码 vendored 在 third_party/spdlog, 不依赖 vcpkg 等包管理器,
//    与本项目"第三方库随仓库走"的做法一致。
// ======================================================================
#ifndef ICLOUDMON_LOG_H
#define ICLOUDMON_LOG_H

#include <QString>

#include "spdlog/spdlog.h"

#include <string>

namespace icloud {
namespace log {

// ── 初始化。必须在 QGuiApplication 构造之后、任何其他模块之前调用。
//   重复调用安全(只生效一次)。返回 false 表示目录不可写(降级为不写文件)。
bool init();

// ─- 进程退出时调用: 刷盘并**关闭日志通道**。
//   调用之后所有日志出口自动变 no-op(见下面 logAt 的说明) —— 退出阶段
//   的析构路径还会写日志, 不关闸就会在空 logger 上崩。
void shutdown();

// 日志目录绝对路径(设置页展示用)
QString logDir();

// 当前日志文件绝对路径
QString currentLogFile();

// ── 语义化快捷入口, 避免到处拼 spdlog::info(...) ──
// 注意: spdlog 内部按 UTF-8 处理, 而 QString::toStdString() 走的是
// 本地代码页(zh-CN 是 GBK)。直接传会把中文写成乱码, 且不可逆。
// 统一走 utf8() 转成 UTF-8 再进 spdlog。
inline std::string utf8(const QString &s)
{
    return s.toUtf8().toStdString();
}

// ── 日志总闸 ────────────────────────────────────────────────────────
//
//  为什么必须有: shutdown() 里调的是 spdlog::shutdown(), 它会把注册表里
//  的 logger —— 包括 default logger —— 一并销毁; 而 spdlog 的自由函数
//  (info/warn/error/critical/log) 内部是
//      default_logger_raw()->log(...)
//  **不做任何空指针检查**。所以 shutdown 之后哪怕再写一句日志, 就是在空
//  logger 上解引用成员, 直接崩。
//
//  这不是理论风险, main() 的顺序恰好必然踩到:
//      写 "[RUN] ==== 退出 (code=0) ===="
//      -> log::shutdown()
//      -> return rc   ← 从这里开始栈对象陆续析构
//  而 Bridge / SidecarClient / IcloudMonitor 等一大批对象的析构路径里都有
//  日志(例如 SidecarClient::shutdown() 要记 "[SIDECAR] 已退出"), 于是每次
//  正常退出都崩一次: WER 留下一个 0xC0000005 转储, 出错地址落在
//      spdlog::logger::log<std::string>
//  (异常信息里"读地址 0x40"就是空 this 的成员偏移; 32 位版偏移是 0x2c,
//   同一个 bug, 从旧版一路带过来)。
//
//  修法: 只在这一处把闸。所有日志出口一律经过 logAt(), 它先问 active();
//  shutdown 之后直接返回 —— 谁也别想再碰到 spdlog。
//  (备选方案是让 log::shutdown() 挪到所有析构之后, 但那只能堵当前这一条
//   路径, 以后任何新增的晚期日志都会复发, 不如闸门彻底。)
bool active();

inline void logAt(spdlog::level::level_enum lvl, const std::string &msg)
{
    if (!active())
        return;
    spdlog::log(lvl, msg);
}

inline void debug(const QString &msg)
{
    logAt(spdlog::level::debug, utf8(msg));
}

inline void info(const QString &msg)
{
    logAt(spdlog::level::info, utf8(msg));
}

inline void warn(const QString &msg)
{
    logAt(spdlog::level::warn, utf8(msg));
}

inline void error(const QString &msg)
{
    logAt(spdlog::level::err, utf8(msg));
}

inline void critical(const QString &msg)
{
    logAt(spdlog::level::critical, utf8(msg));
}

// 带键值对的结构化记录, 用于同步变化这类需要多字段的场景。
// 例: syncChanged("空闲", "上传中", "need_up=3/ 2.1MB")
void syncChanged(const QString &from, const QString &to,
                 const QString &detail);

// 用户操作审计
void userAction(const QString &action, const QString &result = QString());

}  // namespace log
}  // namespace icloud

#endif  // ICLOUDMON_LOG_H