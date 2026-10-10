// ======================================================================
//  BPlist.h  -  最小可用的 Apple 二进制 plist(bplist00)解析器
//
//  为什么要自己写:
//    Qt 5 **没有** plist 解析器(QSettings 只支持 INI/注册表/QConf),
//    QDomDocument 只能解 XML plist, 而 iCloud 写的这个文件是二进制
//    bplist00。所以这里手写一个只覆盖本项目所需类型的最小实现,
//    避免为此引入第三方依赖。
//
//  支持的对象类型(足以读 iCloudWinPref.plist 的 StorageData):
//    dict / array / string(ASCII 与 UTF-16BE) / integer / real /
//    true / false / data(忽略) / date(忽略)
//  遇到不认识的类型就返回 QVariant(), 不会崩溃。
//
//  参考格式(bplist00):
//    头部  "bplist00"
//    Trailer(最后 32 字节): offsetSize(1) refSize(1) numObjects(8)
//                          topObject(8) offsetTableOffset(8)
//    对象表: 每个对象一个标记(高4位=类型, 低4位=长度或索引)
//          偏移表: numObjects 个 offsetSize 字节的大端偏移
// ======================================================================
#pragma once

#include <QByteArray>
#include <QVariant>
#include <QVector>

namespace icloud {

// 解析 bplist00 数据。
// 成功返回根对象(通常是 dict -> QVariantMap);
// 失败或格式不认识返回无效QVariant()。
QVariant parseBplist(const QByteArray &data);

} // namespace icloud