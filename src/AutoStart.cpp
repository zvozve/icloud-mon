#include "AutoStart.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QSettings>

#include "IcloudPaths.h"

namespace icloud {

AutoStart::AutoStart(QObject *parent)
    : QObject(parent)
    , m_key(paths::autoStartRegistryKey())
{
}

QString AutoStart::selfPath()
{
    // QApplication::applicationFilePath() 在 Windows 上返回完整路径,
    // 但含空格时必须加引号, 否则注册表里的命令行会被截断。
    QString p = QCoreApplication::applicationFilePath();
    p.replace(QLatin1Char('/'), QLatin1Char('\\'));
    if (!p.startsWith(QLatin1Char('"')) && p.contains(QLatin1Char(' ')))
        p = QLatin1Char('"') + p + QLatin1Char('"');
    return p;
}

bool AutoStart::isEnabled() const
{
    QSettings reg(m_key, QSettings::NativeFormat);
    return reg.contains(QStringLiteral("iCloudMon"))
        && !reg.value(QStringLiteral("iCloudMon")).toString().isEmpty();
}

QString AutoStart::commandLine() const
{
    QSettings reg(m_key, QSettings::NativeFormat);
    return reg.value(QStringLiteral("iCloudMon")).toString();
}

bool AutoStart::setEnabled(bool on, QString *error)
{
    auto fail = [&](const QString &e) {
        if (error)
            *error = e;
        return false;
    };

    QSettings reg(m_key, QSettings::NativeFormat);
    const QString exe = selfPath();
    if (exe.isEmpty())
        return fail(QStringLiteral("无法确定自身路径"));

    if (on) {
        // 校验用的是未加引号的真实路径 (selfPath 可能包了引号),
        // QString::replace 是非 const, 不能直接在 const QString 上调用,
        // 也不能用"把引号换成空格"这种 hack —— 直接重新取一次裸路径。
        QString bare = QCoreApplication::applicationFilePath();
        bare.replace(QLatin1Char('/'), QLatin1Char('\\'));
        if (!QFileInfo::exists(bare))
            return fail(QStringLiteral("可执行文件不存在: %1").arg(bare));
        reg.setValue(QStringLiteral("iCloudMon"), exe);
    } else {
        reg.remove(QStringLiteral("iCloudMon"));
    }
    reg.sync();

    if (reg.status() != QSettings::NoError)
        return fail(QStringLiteral("写注册表失败: %1").arg(reg.status()));
    return true;
}

}  // namespace icloud
