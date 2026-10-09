// ======================================================================
//  AutoStart.h  -  开机自启动开关 (注册表 HKCU, 免管理员)
// ======================================================================
#ifndef AUTOSTART_H
#define AUTOSTART_H

#include <QObject>
#include <QString>

namespace icloud {

class AutoStart : public QObject
{
    Q_OBJECT

public:
    explicit AutoStart(QObject *parent = nullptr);

    // 当前是否已开启自启动
    Q_INVOKABLE bool isEnabled() const;

    // 设置开关。返回 false 并填 error 表示失败。
    Q_INVOKABLE bool setEnabled(bool on, QString *error = nullptr);

    // 注册表里存的命令行 (诊断用)
    Q_INVOKABLE QString commandLine() const;

    // 应用自身可执行文件完整路径 (带引号)
    static QString selfPath();

private:
    QString m_key;
};

}  // namespace icloud

#endif  // AUTOSTART_H
