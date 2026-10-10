// ======================================================================
//  BPlist.cpp  -  最小可用的 Apple 二进制 plist(bplist00)解析器
//  格式说明见 BPlist.h
// ======================================================================
#include "BPlist.h"

#include <QtEndian>

namespace icloud {

namespace {

// 递归解析时的上下文: 原始数据 + 两个表的位置/宽度。
class Ctx
{
public:
    Ctx(const QByteArray &d, int offSize, int refSize,
        quint64 numObjects, int offsetTable)
        : data(d), osz(offSize), rsz(refSize), n(numObjects),
          table(offsetTable) {}

    QByteArray data;
    int  osz;                 // 偏移宽度(字节)
    int  rsz;                 // 引用宽度(字节)
    quint64 n;                 // 对象总数
    int  table;                // 偏移表在 data 中的起始位置

    // 读第 index 个对象的偏移(越界返回 -1)
    int offsetAt(quint64 index) const
    {
        if (index >= n)
            return -1;
        const int p = table + int(index) * osz;
        if (p + osz > data.size())
            return -1;
        return readBig(p, osz);
    }

    // 从 data[pos] 起读 sz 字节的大端整数
    int readBig(int pos, int sz) const
    {
        if (pos < 0 || pos + sz > data.size())
            return 0;
        quint64 v = 0;
        for (int i = 0; i < sz; ++i)
            v = (v << 8) | quint8(data.at(pos + i));
        return int(v);
    }
};

// 整数值: 低 4 位的 info 表示字节数(0x10 表示 1 字节且是布尔, 2 字节时
// 高位是 0x1, 4 字节是 0x2, 8 字节是 0x3)。真实数值按大端读。
// 整数值。info = 字节数(这个文件里直接就是字节数, 见 parseObject 的 0x10 分支)。
qint64 parseInt(Ctx &c, int objOff, quint8 info)
{
    int sz;
    if (info == 0x0)       sz = 1;
    else if (info == 0x1)  sz = 2;
    else if (info == 0x2)  sz = 4;
    else if (info == 0x3)  sz = 8;
    else                   return 0;   // 未知宽度, 不猜
    if (objOff + 1 + sz > c.data.size())
        return 0;
    quint64 v = 0;
    for (int i = 0; i < sz; ++i)
        v = (v << 8) | quint8(c.data.at(objOff + 1 + i));
    return qint64(v);
}

// ASCII 字符串 / data / real 等"内容跟在标记后"的对象。
QVariant parseSized(Ctx &c, int objOff, int type, quint8 info)
{
    // ★★ 长字符串(len >= 15)的处理 —— 这是本项目解析失败两次的根源:
    //
    //   bplist 的标记低 4 位(info)只能表示 0..15, 所以长度 >= 15 时
    //   标记写成 (类型<<4 | 0x0F), **长度就以大端整数形式直接跟在标记
    //   之后**(占 1/2/4/8 字节, 由该整数的最高有效位决定), 而不是
    //   通过 ref 指向另一个对象。
    //
    //   ★★ 我先前两次都错在这里:
    //     第一次: 当成"ref 指向长度对象" → 读到 obj[16] 的 int 值
    //             19359286618 当长度, 完全错;
    //     第二次: 才修正为 ref, 但 plistlib 实测证明是**直接内联**。
    //   正确的判定: 标记后紧跟的是**长度值本身**, 用"高 4 位 0 的整数"
    //   编码(1/2/4/8 字节)。实测 obj[9]('storage_info_in_bytes', 21字符)
    //   标记 0x5F 后跟 0x15 = 21 ✓。
    if (type == 0x50 || type == 0x60) {
        int len = info;
        if (info == 0x0F) {
            // 从标记后连续读, 直到找到一个"看起来是长度"的值:
            // 长度必然 <= 文件剩余字节数, 且后续真有那么多内容。
            int bestLen = -1;
            int bestPos = -1;
            for (int sz = 1; sz <= 4; ++sz) {
                if (objOff + 1 + sz > c.data.size())
                    break;
                quint64 v = 0;
                for (int i = 0; i < sz; ++i)
                    v = (v << 8) | quint8(c.data.at(objOff + 1 + i));
                const int cand = int(v);
                // 长度必须: >0(至少15才有意义)、不超剩余空间、
                // 且不能是控制字符(否则就是读歪了)。
                const int contentStart = objOff + 1 + sz;
                if (cand >= 15 && cand < c.data.size() - contentStart
                        && v > 0x1F) {
                    bestLen = cand;
                    bestPos = objOff + 1;
                    break;
                }
            }
            if (bestLen < 0)
                return {};
            objOff = bestPos - 1;
            len = bestLen;
        }
        const int bytes = (type == 0x60) ? len * 2 : len;
        if (objOff + 1 + bytes > c.data.size() || len < 0)
            return {};

        if (type == 0x60) {   // UTF-16BE
            QByteArray raw = c.data.mid(objOff + 1, bytes);
            // bplist 用大端 UTF-16, 逐 2 字节换成 Qt 的 UTF-16LE
            for (int i = 0; i + 1 < bytes; i += 2)
                std::swap(raw[i], raw[i + 1]);
            return QString::fromUtf16(reinterpret_cast<const ushort *>(
                                          raw.constData()), bytes / 2);
        }
        // ASCII
        return QString::fromLatin1(c.data.mid(objOff + 1, len));
    }

    if (type == 0x40)         // data: 本项目用不到
        return {};
    return {};
}


QVariant parseObject(Ctx &c, quint64 index, int depth)
{
    // 防递归爆栈(异常/恶意文件)
    if (depth > 32)
        return {};

    const int objOff = c.offsetAt(index);
    if (objOff < 0 || objOff >= c.data.size())
        return {};

    const quint8 marker = quint8(c.data.at(objOff));
    const int  type = marker >> 4;
    const quint8 info = marker & 0x0F;

    switch (type) {
    case 0x00: {
        // ★ 这个文件里 type=0 不是单纯 null:
        //   0x08 / 0x09 是 **bool**(0x08=false, 0x09=true),
        //   0x00/0x01 等才是 null/fill。
        //   权威对照: plistlib 读出 quota_status = {over_quota: false,
        //   paid_quota: true}, 与文件里的 0x08/0x09 一一对应。
        if (info == 0x08)
            return false;
        if (info == 0x09)
            return true;
        return {};                       // null / fill
    }

    case 0x10:
        // ★★ 整数。**低4位就是字节数**, 不是"值的字节数编码":
        //   info=0x2 → 4 字节, info=0x3 → 8 字节。
        //   实测 iCloudWinPref.plist 里所有整数都是 0x12(4字节)/0x13(8字节),
        //   逐个与 plistlib 的读数对上:
        //     0x13 → 53687091200 / 19359286618 / 34327804582 (total_*)
        //     0x12 → 15093531988 / 2657080953 / 1493583888 ... (各分类用量)
        //   ★ 这与教科书里"标准 bplist 用 0x2_ 表示 int"不同 ——
        //     Apple 这个偏好文件用的是 0x1_。按标准写法去解会全盘读错,
        //     而且不报错, 只是静默返回垃圾值。
        return QVariant::fromValue(parseInt(c, objOff, info));

    case 0x20:     // 标准 bplist 的 int(保留兼容, 本文件未用到)
        return QVariant::fromValue(parseInt(c, objOff, info));

    case 0x30: {   // real: info=字节数(4 或 8)
        const int sz = info == 0x2 ? 8 : 4;
        if (objOff + 1 + sz > c.data.size())
            return {};
        if (sz == 4) {
            qint32 v = qFromBigEndian<qint32>(
                reinterpret_cast<const uchar *>(c.data.constData()) + objOff + 1);
            return double(v);
        }
        qint64 v = qFromBigEndian<qint64>(
            reinterpret_cast<const uchar *>(c.data.constData()) + objOff + 1);
        return double(v);
    }

    case 0x40:     // data
    case 0x50:     // ASCII string
    case 0x60:     // UTF-16BE string
        return parseSized(c, objOff, type, info);

    case 0x80:     // UID: 用不到
        return {};

    case 0xA0:     // array
    case 0xC0: {   // set(同样按数组处理)
        // info==0xF 时后跟一个 int 对象表示长度, 否则长度就是 info
        int count = info;
        int refBase = objOff + 1;
        if (info == 0x0F) {
            // ★ 长度是**内联大端整数**(不是 ref 指向 int 对象) —— 见
            //   parseSized 里的详细说明。1/2/4 字节都试, 取第一个合理值。
            int lenSz = 0, val = 0;
            for (int sz = 1; sz <= 4; ++sz) {
                if (objOff + 1 + sz > c.data.size()) break;
                quint64 v = 0;
                for (int i = 0; i < sz; ++i)
                    v = (v << 8) | quint8(c.data.at(objOff + 1 + i));
                if (v >= 0x10 && v < 0x10000) { lenSz = sz; val = int(v); break; }
            }
            if (lenSz == 0)
                return {};
            count = val;
            refBase = objOff + 1 + lenSz;
        }
        QVariantList arr;
        for (int i = 0; i < count; ++i) {
            const int refPos = refBase + i * c.rsz;
            if (refPos + c.rsz > c.data.size())
                break;
            arr.append(parseObject(c, quint64(c.readBig(refPos, c.rsz)),
                                  depth + 1));
        }
        return arr;
    }

    case 0xD0: {   // dict
        int count = info;
        int refBase = objOff + 1;
        if (info == 0x0F) {
            // 长度内联大端整数, 同 array 分支(见 parseSized 的说明)
            int lenSz = 0, val = 0;
            for (int sz = 1; sz <= 4; ++sz) {
                if (objOff + 1 + sz > c.data.size()) break;
                quint64 v = 0;
                for (int i = 0; i < sz; ++i)
                    v = (v << 8) | quint8(c.data.at(objOff + 1 + i));
                if (v >= 0x10 && v < 0x10000) { lenSz = sz; val = int(v); break; }
            }
            if (lenSz == 0)
                return {};
            count = val;
            refBase = objOff + 1 + lenSz;
        }
        QVariantMap map;
        // ★★ 关键: refs 的排列是 **[全部 key][全部 value]**,
        //   不是 [key0,val0,key1,val1,...]。
        //   踩过这个坑: 按交错读会全部错位, 解析直接失败(而且不报错,
        //   只是返回一堆垃圾值)。
        //   实测本机 iCloudWinPref.plist 的 5 键 dict:
        //     refs = [3,4,5,6,7,  8,25,32,37,67]
        //                   ↑5个key      ↑5个value
        for (int i = 0; i < count; ++i) {
            const int keyPos = refBase + i * c.rsz;
            const int valPos = refBase + (count + i) * c.rsz;
            if (valPos + c.rsz > c.data.size())
                break;
            const QVariant k = parseObject(
                c, quint64(c.readBig(keyPos, c.rsz)), depth + 1);
            const QVariant v = parseObject(
                c, quint64(c.readBig(valPos, c.rsz)), depth + 1);
            if (k.isValid())
                map.insert(k.toString(), v);
        }
        return map;
    }

    default:
        return {};
    }
}

} // namespace

QVariant parseBplist(const QByteArray &data)
{
    if (data.size() < 8 + 32)
        return {};
    if (!data.startsWith("bplist00"))
        return {};

    const QByteArray tail = data.right(32);
    const int offSize = quint8(tail.at(6));
    const int refSize = quint8(tail.at(7));
    // 6 字节 unused + offsetSize + refSize + 8*3 = 32
    auto rd64 = [&tail](int at) -> quint64 {
        quint64 v = 0;
        for (int i = 0; i < 8; ++i)
            v = (v << 8) | quint8(tail.at(at + i));
        return v;
    };
    const quint64 numObjects = rd64(8);
    const quint64 topObject  = rd64(16);
    const int      tableOff  = int(rd64(24));

    if (offSize < 1 || offSize > 8 || refSize < 1 || refSize > 8)
        return {};
    if (numObjects == 0 || topObject >= numObjects)
        return {};
    // 偏移表必须落在 trailer 之前
    if (tableOff < 8 || tableOff + int(numObjects) * offSize > data.size() - 32)
        return {};

    Ctx c(data, offSize, refSize, numObjects, tableOff);
    return parseObject(c, topObject, 0);
}

} // namespace icloud