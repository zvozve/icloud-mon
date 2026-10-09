// ======================================================================
//  SettingsPage.qml  -  设置页（独立页面，不再是滑出面板）
//
//  改造自原来的覆盖层版本, 去掉了:
//    - 半透明遮罩 + MouseArea 点击关闭
//    - 滑入/滑出动画与面板圆角、关闭按钮
//  因为现在由左侧导航切页, 不再需要"弹出再收起"这层交互。
//
//  内容是竖长的一列, 所以包在 Flickable 里; 并限宽居中
//  (UWP 设置页就是这个做法, 一行拉太长不好读)。
// ======================================================================
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Dialogs 1.3
import QtQuick.Layouts 1.15

Item {
    id: page

    // 默认启动页下拉的模型: 首页 + 中间四类(按当前侧边栏顺序) + 设置。
    // 顺序与侧边栏保持一致, 否则下拉里的顺序和实际导航对不上。
    readonly property var pageModel: {
        var m = [ { key: "home", name: "首页" } ]
        var o = Bridge.navOrder
        for (var i = 0; i < o.length; ++i)
            m.push({ key: o[i], name: Bridge.categoryName(o[i]) })
        m.push({ key: "settings", name: "设置" })
        return m
    }

    Flickable {
        id: body
        anchors.fill: parent
        contentWidth: width
        contentHeight: Math.max(height, col.implicitHeight + Theme.spL * 2)
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ScrollBar.vertical: ScrollBar {
            policy: body.contentHeight > body.height + 1 ? ScrollBar.AsNeeded
                                                        : ScrollBar.AlwaysOff
        }

        ColumnLayout {
            id: col
            // 限宽并居中。这里没有 fillHeight 子项, 所以不必给确定高度。
            width: Math.min(body.width - Theme.spL * 2, 640)
            x: (body.width - width) / 2
            y: Theme.spL
            spacing: Theme.spL

            Label {
                text: "设置"
                color: Theme.textHi
                font.pixelSize: Theme.fsBig
                font.bold: true
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

            // ══════════════════════════════════════════════════
            //  开机自启动
            // ══════════════════════════════════════════════════
            RowLayout {
                Layout.fillWidth: true

                ColumnLayout {
                    spacing: 1
                    Layout.fillWidth: true
                    Label {
                        text: "开机自启动"
                        color: Theme.textHi
                        font.pixelSize: Theme.fsBody
                        font.bold: true
                    }
                    Label {
                        text: "登录 Windows 后自动运行本监控程序"
                        color: Theme.textLo
                        font.pixelSize: Theme.fsSmall
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }

                Switch {
                    id: autoStartSwitch
                    checked: Bridge.autoStart
                    onToggled: {
                        // 后端写注册表可能失败(如权限), 失败时回滚 UI
                        if (!Bridge.setAutoStart(checked))
                            checked = Bridge.autoStart
                    }

                    contentItem: Rectangle {
                        implicitWidth: 42
                        implicitHeight: 20
                        radius: 10
                        color: autoStartSwitch.checked ? Theme.ok : Theme.bgInput
                        border.color: autoStartSwitch.checked
                                     ? Theme.ok : Theme.borderHi
                        Behavior on color { ColorAnimation { duration: 160 } }

                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            x: autoStartSwitch.checked
                               ? parent.width - width - 2 : 2
                            width: 16; height: 16; radius: 8
                            color: "#ffffff"
                            Behavior on x {
                                NumberAnimation { duration: 160
                                                  easing.type: Easing.OutCubic }
                            }
                        }
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

            // ══════════════════════════════════════════════════
            //  刷新间隔
            // ══════════════════════════════════════════════════
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: "刷新间隔"
                        color: Theme.textHi
                        font.pixelSize: Theme.fsBody
                        font.bold: true
                        Layout.fillWidth: true
                    }
                    Label {
                        text: Bridge.refreshSec + " 秒"
                        color: Theme.info
                        font.pixelSize: Theme.fsBody
                        font.family: "Consolas, monospace"
                    }
                }

                Slider {
                    id: refreshSlider
                    Layout.fillWidth: true
                    from: 1
                    to: 120
                    stepSize: 1
                    value: Bridge.refreshSec
                    onMoved: Bridge.refreshSec = Math.round(value)

                    // 间隔越大越省资源, 但反应越慢
                    readonly property string hint: value <= 2 ? "实时(较耗资源)"
                        : value <= 10 ? "推荐"
                        : value <= 30 ? "省资源" : "很省资源(反应慢)"

                    background: Rectangle {
                        x: refreshSlider.leftPadding
                        y: refreshSlider.topPadding
                           + refreshSlider.availableHeight / 2 - height / 2
                        width: refreshSlider.availableWidth
                        height: 4
                        radius: 2
                        color: Theme.bgInput
                        Rectangle {
                            width: refreshSlider.visualPosition * parent.width
                            height: parent.height
                            radius: parent.radius
                            color: Theme.info
                        }
                    }
                    handle: Rectangle {
                        x: refreshSlider.leftPadding + refreshSlider.visualPosition
                           * (refreshSlider.availableWidth - width)
                        y: refreshSlider.topPadding
                           + refreshSlider.availableHeight / 2 - height / 2
                        width: 14; height: 14; radius: 7
                        color: refreshSlider.pressed ? Theme.infoHi : Theme.info
                        border.color: Theme.bgPanel
                        border.width: 2
                    }
                }

                Label {
                    text: refreshSlider.hint
                    color: Theme.textLo
                    font.pixelSize: Theme.fsTiny
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

            // ══════════════════════════════════════════════════
            //  卡死阈值
            // ══════════════════════════════════════════════════
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: "卡死判定阈值"
                        color: Theme.textHi
                        font.pixelSize: Theme.fsBody
                        font.bold: true
                        Layout.fillWidth: true
                    }
                    Label {
                        text: Bridge.stallSec + " 秒"
                        color: Theme.warn
                        font.pixelSize: Theme.fsBody
                        font.family: "Consolas, monospace"
                    }
                }

                Slider {
                    id: stallSlider
                    Layout.fillWidth: true
                    from: 30
                    to: 1800
                    stepSize: 30
                    value: Bridge.stallSec
                    onMoved: Bridge.stallSec = Math.round(value / 30) * 30

                    readonly property string hint:
                        value <= 120 ? "灵敏(易误报)"
                        : value <= 420 ? "推荐, 5 分钟内无变化即告警"
                        : "迟钝"

                    background: Rectangle {
                        x: stallSlider.leftPadding
                        y: stallSlider.topPadding
                           + stallSlider.availableHeight / 2 - height / 2
                        width: stallSlider.availableWidth
                        height: 4
                        radius: 2
                        color: Theme.bgInput
                        Rectangle {
                            width: stallSlider.visualPosition * parent.width
                            height: parent.height
                            radius: parent.radius
                            color: Theme.warn
                        }
                    }
                    handle: Rectangle {
                        x: stallSlider.leftPadding + stallSlider.visualPosition
                           * (stallSlider.availableWidth - width)
                        y: stallSlider.topPadding
                           + stallSlider.availableHeight / 2 - height / 2
                        width: 14; height: 14; radius: 7
                        color: stallSlider.pressed ? "#f0b849" : Theme.warn
                        border.color: Theme.bgPanel
                        border.width: 2
                    }
                }

                Label {
                    text: stallSlider.hint
                    color: Theme.textLo
                    font.pixelSize: Theme.fsTiny
                }

                Label {
                    text: "判定规则: 有任务在跑, 但队列计数持续不变 = 卡死。空闲不判定。"
                    color: Theme.textLo
                    font.pixelSize: Theme.fsTiny
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

            // ══════════════════════════════════════════════════
            //  同步根目录
            // ══════════════════════════════════════════════════
            RowLayout {
                Layout.fillWidth: true

                ColumnLayout {
                    spacing: 1
                    Layout.fillWidth: true
                    Label {
                        text: "同步根目录"
                        color: Theme.textHi
                        font.pixelSize: Theme.fsBody
                        font.bold: true
                    }
                    Label {
                        text: "点「浏览」选择 iCloud Drive 本地文件夹"
                        color: Theme.textLo
                        font.pixelSize: Theme.fsSmall
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    // 当前值走 Bridge 属性而非 DriveRoot 上下文属性 ——
                    // 后者是启动时的快照, 改完不会刷新。
                    Label {
                        text: Bridge.driveRoot
                        color: Theme.info
                        font.pixelSize: Theme.fsTiny
                        font.family: "Consolas, monospace"
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                }

                Button {
                    id: browseDriveBtn
                    text: "浏览"
                    onClicked: driveDialog.open()
                    contentItem: Label {
                        text: browseDriveBtn.text
                        color: browseDriveBtn.hovered ? Theme.info : Theme.textMid
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.pixelSize: Theme.fsSmall
                    }
                    background: Rectangle {
                        implicitWidth: 56
                        implicitHeight: 28
                        radius: Theme.radius
                        color: browseDriveBtn.hovered ? Theme.bgHover : "transparent"
                        border.color: browseDriveBtn.hovered ? Theme.info : Theme.borderHi
                    }
                }

                Button {
                    id: resetDriveBtn
                    text: "默认"
                    visible: Bridge.driveRoot !== "C:/Container/Cloud/iCloudDrive"
                    onClicked: Bridge.resetDriveRoot()
                    ToolTip.visible: hovered
                    ToolTip.text: "恢复内置默认路径"
                    ToolTip.delay: 600
                    contentItem: Label {
                        text: resetDriveBtn.text
                        color: resetDriveBtn.hovered ? Theme.warn : Theme.textMid
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.pixelSize: Theme.fsSmall
                    }
                    background: Rectangle {
                        implicitWidth: 48
                        implicitHeight: 28
                        radius: Theme.radius
                        color: resetDriveBtn.hovered ? Theme.bgHover : "transparent"
                        border.color: resetDriveBtn.hovered ? Theme.warn : Theme.borderHi
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

            // ══════════════════════════════════════════════════
            //  运行日志
            // ══════════════════════════════════════════════════
            RowLayout {
                Layout.fillWidth: true

                ColumnLayout {
                    spacing: 1
                    Layout.fillWidth: true
                    Label {
                        text: "运行日志"
                        color: Theme.textHi
                        font.pixelSize: Theme.fsBody
                        font.bold: true
                    }
                    Label {
                        text: "按天记录同步变化、运行状态与操作记录"
                        color: Theme.textLo
                        font.pixelSize: Theme.fsSmall
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Label {
                        text: Bridge.logFile
                        color: Theme.textLo
                        font.pixelSize: Theme.fsTiny
                        font.family: "Consolas, monospace"
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                }

                Button {
                    id: logBtn
                    text: "打开"
                    onClicked: Bridge.openLogDir()
                    contentItem: Label {
                        text: logBtn.text
                        color: logBtn.hovered ? Theme.info : Theme.textMid
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.pixelSize: Theme.fsSmall
                    }
                    background: Rectangle {
                        implicitWidth: 60
                        implicitHeight: 28
                        radius: Theme.radius
                        color: logBtn.hovered ? Theme.bgHover : "transparent"
                        border.color: logBtn.hovered ? Theme.info : Theme.borderHi
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

            // ══════════════════════════════════════════════════
            //  系统通知
            // ══════════════════════════════════════════════════
            RowLayout {
                Layout.fillWidth: true

                ColumnLayout {
                    spacing: 1
                    Layout.fillWidth: true
                    Label {
                        text: "系统通知"
                        color: Theme.textHi
                        font.pixelSize: Theme.fsBody
                        font.bold: true
                    }
                    Label {
                        text: "iCloud 卡死时弹 Windows 通知，窗口最小化也能收到"
                        color: Theme.textLo
                        font.pixelSize: Theme.fsSmall
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }

                Button {
                    id: testNotifyBtn
                    text: "测试通知"
                    onClicked: Bridge.testSystemNotification()
                    ToolTip.visible: hovered
                    ToolTip.text: "立即弹一条测试通知，确认系统通知是否正常"
                    ToolTip.delay: 600
                    contentItem: Label {
                        text: testNotifyBtn.text
                        color: testNotifyBtn.hovered ? Theme.info : Theme.textMid
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.pixelSize: Theme.fsSmall
                    }
                    background: Rectangle {
                        implicitWidth: 76
                        implicitHeight: 28
                        radius: Theme.radius
                        color: testNotifyBtn.hovered ? Theme.bgHover : "transparent"
                        border.color: testNotifyBtn.hovered ? Theme.info : Theme.borderHi
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

            // ══════════════════════════════════════════════════
            //  类别启用状态（手动指定）
            // ══════════════════════════════════════════════════
            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                Label {
                    text: "类别启用状态"
                    color: Theme.textHi
                    font.pixelSize: Theme.fsBody
                    font.bold: true
                }
                Label {
                    Layout.fillWidth: true
                    text: "自动检测只对云盘和相册可靠；密码和书签检测不出来"
                          + "（真正的开关存在被 iCloud 独占锁定的配置里）。"
                          + "请在下面手动指定，点「自动」可恢复为自动检测。"
                    color: Theme.textLo
                    font.pixelSize: Theme.fsTiny
                    wrapMode: Text.WordWrap
                }

                Repeater {
                    model: Bridge.categories

                    delegate: RowLayout {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        spacing: Theme.sp

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Label {
                                text: modelData.name
                                      + (modelData.userSet ? "（手动指定）"
                                                           : "（自动检测）")
                                color: Theme.textHi
                                font.pixelSize: Theme.fsSmall
                                font.bold: true
                            }
                            Label {
                                Layout.fillWidth: true
                                text: "自动检测："
                                      + (modelData.autoOn ? "已启用" : "未启用")
                                      + " · " + modelData.autoWhy
                                color: Theme.textLo
                                font.pixelSize: Theme.fsTiny
                                elide: Text.ElideRight
                            }
                        }

                        Button {
                            id: autoBtn
                            text: "自动"
                            enabled: modelData.userSet === true
                            onClicked: Bridge.clearCategoryEnabled(modelData.key)
                            ToolTip.visible: hovered
                            ToolTip.text: "清除手动指定，回退到自动检测"
                            ToolTip.delay: 600
                            contentItem: Label {
                                text: autoBtn.text
                                color: autoBtn.enabled
                                       ? (autoBtn.hovered ? Theme.info : Theme.textMid)
                                       : Theme.textLo
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                font.pixelSize: Theme.fsSmall
                            }
                            background: Rectangle {
                                implicitWidth: 48
                                implicitHeight: 28
                                radius: Theme.radius
                                color: autoBtn.hovered ? Theme.bgHover : "transparent"
                                border.color: autoBtn.enabled ? Theme.borderHi
                                                              : Theme.border
                            }
                        }

                        Switch {
                            checked: modelData.enabled === true
                            onToggled: Bridge.setCategoryEnabled(modelData.key,
                                                                 checked)

                            contentItem: Rectangle {
                                implicitWidth: 42
                                implicitHeight: 20
                                radius: 10
                                color: parent.checked ? Theme.ok : Theme.bgInput
                                border.color: parent.checked ? Theme.ok
                                                             : Theme.borderHi
                                Behavior on color { ColorAnimation { duration: 160 } }

                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    x: parent.checked ? parent.width - width - 2 : 2
                                    width: 16; height: 16; radius: 8
                                    color: "#ffffff"
                                    Behavior on x {
                                        NumberAnimation { duration: 160
                                                          easing.type: Easing.OutCubic }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ══════════════════════════════════════════════════
            //  侧边栏顺序
            // ══════════════════════════════════════════════════
            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: "侧边栏顺序"
                        color: Theme.textHi
                        font.pixelSize: Theme.fsBody
                        font.bold: true
                        Layout.fillWidth: true
                    }
                    Button {
                        id: resetOrderBtn
                        text: "恢复默认"
                        onClicked: Bridge.resetNavOrder()
                        contentItem: Label {
                            text: resetOrderBtn.text
                            color: resetOrderBtn.hovered ? Theme.info : Theme.textMid
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            font.pixelSize: Theme.fsTiny
                        }
                        background: Rectangle {
                            implicitWidth: 72
                            implicitHeight: 24
                            radius: Theme.radius
                            color: resetOrderBtn.hovered ? Theme.bgHover : "transparent"
                            border.color: Theme.borderHi
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: "首页固定在第一位、设置固定在最后一位，都不可调；"
                          + "中间四类可用 ↑ ↓ 调整。"
                    color: Theme.textLo
                    font.pixelSize: Theme.fsTiny
                    wrapMode: Text.WordWrap
                }

                Repeater {
                    model: Bridge.navOrder

                    delegate: RowLayout {
                        required property string modelData
                        required property int index
                        Layout.fillWidth: true
                        spacing: 6

                        Label {
                            text: (index + 1) + ".  " + Bridge.categoryName(modelData)
                            color: Theme.textMid
                            font.pixelSize: Theme.fsSmall
                            Layout.fillWidth: true
                        }

                        Button {
                            id: upBtn
                            text: "↑"
                            enabled: index > 0
                            onClicked: Bridge.moveCategory(modelData, -1)
                            ToolTip.visible: hovered
                            ToolTip.text: "上移"
                            ToolTip.delay: 600
                            contentItem: Label {
                                text: upBtn.text
                                color: upBtn.enabled ? (upBtn.hovered ? Theme.info
                                                                      : Theme.textMid)
                                                     : Theme.textLo
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                font.pixelSize: Theme.fsSmall
                            }
                            background: Rectangle {
                                implicitWidth: 30
                                implicitHeight: 26
                                radius: Theme.radius
                                color: upBtn.hovered ? Theme.bgHover : "transparent"
                                border.color: upBtn.enabled ? Theme.borderHi
                                                            : Theme.border
                            }
                        }

                        Button {
                            id: downBtn
                            text: "↓"
                            enabled: index < Bridge.navOrder.length - 1
                            onClicked: Bridge.moveCategory(modelData, 1)
                            ToolTip.visible: hovered
                            ToolTip.text: "下移"
                            ToolTip.delay: 600
                            contentItem: Label {
                                text: downBtn.text
                                color: downBtn.enabled ? (downBtn.hovered ? Theme.info
                                                                          : Theme.textMid)
                                                       : Theme.textLo
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                font.pixelSize: Theme.fsSmall
                            }
                            background: Rectangle {
                                implicitWidth: 30
                                implicitHeight: 26
                                radius: Theme.radius
                                color: downBtn.hovered ? Theme.bgHover : "transparent"
                                border.color: downBtn.enabled ? Theme.borderHi
                                                              : Theme.border
                            }
                        }
                    }
                }
            }

            // ══════════════════════════════════════════════════
            //  默认启动页
            // ══════════════════════════════════════════════════
            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

            RowLayout {
                Layout.fillWidth: true

                ColumnLayout {
                    spacing: 1
                    Layout.fillWidth: true
                    Label {
                        text: "默认启动页"
                        color: Theme.textHi
                        font.pixelSize: Theme.fsBody
                        font.bold: true
                    }
                    Label {
                        text: "下次启动时打开哪一页"
                        color: Theme.textLo
                        font.pixelSize: Theme.fsSmall
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }

                ComboBox {
                    id: defaultPageBox
                    Layout.preferredWidth: 160
                    model: page.pageModel
                    textRole: "name"
                    currentIndex: {
                        var m = page.pageModel
                        for (var i = 0; i < m.length; ++i)
                            if (m[i].key === Bridge.defaultPage)
                                return i
                        return 0
                    }
                    onActivated: Bridge.defaultPage = page.pageModel[index].key
                }
            }

            // ══════════════════════════════════════════════════
            //  路径信息 (只读)
            // ══════════════════════════════════════════════════
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: pathCol.implicitHeight + 12
                color: Theme.bgInput
                radius: 3
                border.color: Theme.border
                border.width: 1

                ColumnLayout {
                    id: pathCol
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 2

                    Label {
                        text: "同步根: " + Bridge.driveRoot
                        color: Theme.textLo
                        font.pixelSize: Theme.fsTiny
                        font.family: "Consolas, monospace"
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                    Label {
                        text: "历史: " + Bridge.stateFilePath()
                        color: Theme.textLo
                        font.pixelSize: Theme.fsTiny
                        font.family: "Consolas, monospace"
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                    Label {
                        text: "版本: " + AppVersion + "  |  iCloud " + Bridge.clientVersion
                        color: Theme.textLo
                        font.pixelSize: Theme.fsTiny
                    }
                }
            }

            Item { Layout.preferredHeight: Theme.spL }
        }

        // ── 目录选择对话框 ──
        // 注意: QtQuick.Dialogs 的 FileDialog 用 selectFolder 选目录,
        // 没有 folderMode 属性 (那是 Qt.labs.platform 的 API, 两者不同)。
        FileDialog {
            id: driveDialog
            title: "选择 iCloud Drive 目录"
            selectFolder: true
            // 从当前路径打开, 避免每次都从盘符根开始翻
            folder: "file:///" + Bridge.driveRoot
            onAccepted: Bridge.setDriveRoot(selectedFile.toString())
        }
    }
}
