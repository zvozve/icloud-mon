// ======================================================================
//  make_ico  -  把 SVG 渲染成多尺寸 .ico(构建期小工具, 不随主程序发布)
//
//  为什么需要: Windows 的 exe 图标是**嵌在 PE 资源里的 .ico**, 和运行时
//  setWindowIcon(用 qsvg 加载 icloud.svg)是两回事 —— Explorer/任务栏/Alt-Tab
//  看的是 PE 资源。而 .ico 必须是位图(BMP/PNG), 不能直接塞 SVG。
//  本机没有 ImageMagick/Pillow/cairosvg, 但有 Qt5Svg, 所以用 Qt 渲染。
//
//  输出: 一个包含 16/24/32/48/64/128/256 多张 PNG 的 .ico
//        (Vista+ 支持 PNG-in-ICO, 直接把 PNG 字节塞进 ICO 容器即可)
//
//  用法: make_ico <in.svg> <out.ico>
// ======================================================================
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>
#include <QBuffer>
#include <QFile>
#include <QByteArray>
#include <QVector>
#include <QDebug>

static void put8(QFile &f, quint8 v)  { f.write((const char *)&v, 1); }
static void put16(QFile &f, quint16 v){ f.write((const char *)&v, 2); }
static void put32(QFile &f, quint32 v){ f.write((const char *)&v, 4); }

static bool writeMultiSizeIco(const QString &outPath,
                              const QVector<QByteArray> &pngs,
                              const QVector<int> &sizes)
{
    QFile f(outPath);
    if (!f.open(QIODevice::WriteOnly))
        return false;

    // ICONDIR
    put16(f, 0);                                   // reserved
    put16(f, 1);                                   // type = icon
    put16(f, (quint16)pngs.size());                // count
    // ICONDIRENTRY[]
    quint32 offset = 6 + 16 * (quint32)pngs.size();
    for (int i = 0; i < pngs.size(); ++i) {
        int s = sizes.at(i);
        put8 (f, (quint8)(s >= 256 ? 0 : s));      // 0 表示 256
        put8 (f, (quint8)(s >= 256 ? 0 : s));
        put8 (f, 0);                              // 颜色数(0=真彩)
        put8 (f, 0);                              // reserved
        put16(f, 1);                              // planes
        put16(f, 32);                             // bpp
        put32(f, (quint32)pngs.at(i).size());      // 数据大小
        put32(f, offset);                         // 数据偏移
        offset += (quint32)pngs.at(i).size();
    }
    // 图像数据(PNG)
    for (const QByteArray &p : pngs)
        f.write(p);
    f.close();
    return true;
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    if (argc < 3) {
        qWarning("用法: make_ico <in.svg> <out.ico>");
        return 1;
    }
    const QString in  = QString::fromLocal8Bit(argv[1]);
    const QString out = QString::fromLocal8Bit(argv[2]);

    QFile f(in);
    if (!f.open(QIODevice::ReadOnly)) { qWarning("打不开 %s", qPrintable(in)); return 1; }
    QByteArray svg = f.readAll();
    f.close();

    QSvgRenderer renderer(svg);
    if (!renderer.isValid()) { qWarning("SVG 解析失败: %s", qPrintable(in)); return 1; }

    const QVector<int> sizes = {16, 24, 32, 48, 64, 128, 256};
    QVector<QByteArray> pngs;
    for (int s : sizes) {
        QImage img(s, s, QImage::Format_ARGB32);
        img.fill(Qt::transparent);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        renderer.render(&p, QRectF(0, 0, s, s));
        p.end();
        QByteArray ba;
        QBuffer buf(&ba);
        buf.open(QIODevice::WriteOnly);
        img.save(&buf, "PNG");
        buf.close();
        if (ba.isEmpty()) { qWarning("渲染 %dx%d 失败", s, s); return 1; }
        pngs.append(ba);
    }

    if (!writeMultiSizeIco(out, pngs, sizes)) { qWarning("写 ICO 失败"); return 1; }
    qInfo("已生成 %s (%d 个尺寸)", qPrintable(out), pngs.size());
    return 0;
}