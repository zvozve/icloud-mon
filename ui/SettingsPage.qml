// ======================================================================
//  SettingsPage.qml  -  设置页 (覆盖层)
//
//  包含:
//    - 开机自启动开关 (注册表 HKCU\...\Run, 免管理员)
//    - 刷新间隔
//    - 卡死判定阈值
//    - 历史基线管理
//    - 路径信息 (只读展示, 便于排查)
// ======================================================================
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: page
    anchors.fill: parent
    color: Qt.rgba(0, 0, 0, 0.55)
    // 初始隐藏: 否则遮罩会一直吞掉主界面的鼠标事件
    visible: false
    opacity: 0

    // 点击遮罩关闭
    MouseArea {
        anchors.fill: parent
        onClicked: page.close()
    }

    // 当前是滑入还是滑出, 动画的 to 值由它决定
    property bool opening: true

    function open()  { opening = true;  visible = true; slideAnim.restart() }
    function close() { opening = false; slideAnim.restart() }

    // ── 滑入/滑出动画 (两处动画合并到这里, 避免重复 id) ──
    ParallelAnimation {
        id: slideAnim
        NumberAnimation {
            target: panel
            property: "x"
            to: page.opening ? page.width - panel.width : page.width
            duration: 200
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            target: page
            property: "opacity"
            to: page.opening ? 1 : 0
            duration: page.opening ? 180 : 160
        }
        // 滑出结束后真正隐藏, 否则遮罩继续拦截点击
        onStopped: if (!page.opening) page.visible = false
    }

    Component.onCompleted: panel.x = width

    // ══════════════════════════════════════════════════════════════
    //  面板
    // ══════════════════════════════════════════════════════════════
    Rectangle {
        id: panel
        width: 460
        height: Math.min(page.height - 40, content.implicitHeight + Theme.spXL * 2)
        x: page.width
        color: Theme.bgPanel
        border.color: Theme.border
        border.width: 1
        radius: Theme.radius

        ColumnLayout {
            id: content
            anchors.fill: parent
            anchors.margins: Theme.spXL
            spacing: Theme.spL

            // ── 标题栏 ──
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: "设置"
                    color: Theme.textHi
                    font.pixelSize: Theme.fsBig
                    font.bold: true
                }
                Item { Layout.fillWidth: true }
                Button {
                    width: 28; height: 28
                    onClicked: page.close()
                    contentItem: Text {
                        text: "✕"; color: Theme.textMid
                        font.pixelSize: Theme.fsBody
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: Theme.radius
                        color: parent.hovered ? Theme.bgHover : "transparent"
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

            // ══════════════════════════════════════════════════
            //  开机自启动
            // ══════════════════════════════════════════════════
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6

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

                // 实际写入的命令行, 便于确认
                Rectangle {
                    Layout.fillWidth: true
                    height: autoStartCmd.implicitHeight + 10
                    visible: Bridge.autoStart
                    color: Theme.bgInput
                    radius: 3
                    border.color: Theme.border
                    border.width: 1

                    Label {
                        id: autoStartCmd
                        anchors.fill: parent
                        anchors.margins: 5
                        text: "注册表: HKCU\\...\\Run  ->  iCloudMon"
                        color: Theme.textLo
                        font.pixelSize: Theme.fsTiny
                        font.family: "Consolas, monospace"
                        elide: Text.ElideRight
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
            //  历史基线
            // ══════════════════════════════════════════════════
            RowLayout {
                Layout.fillWidth: true

                ColumnLayout {
                    spacing: 1
                    Layout.fillWidth: true
                    Label {
                        text: "卡死检测基线"
                        color: Theme.textHi
                        font.pixelSize: Theme.fsBody
                        font.bold: true
                    }
                    Label {
                        text: "每次运行自动记录。首次使用需积累样本才能判定卡死。"
                        color: Theme.textLo
                        font.pixelSize: Theme.fsSmall
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }

                Button {
                    id: resetBtn
                    text: "清空"
                    onClicked: Bridge.resetHistory()
                    contentItem: Label {
                        text: resetBtn.text
                        color: resetBtn.hovered ? Theme.err : Theme.textMid
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.pixelSize: Theme.fsSmall
                    }
                    background: Rectangle {
                        implicitWidth: 60
                        implicitHeight: 28
                        radius: Theme.radius
                        color: resetBtn.hovered ? Theme.bgHover : "transparent"
                        border.color: resetBtn.hovered ? Theme.err : Theme.borderHi
                    }
                }
            }

            // ══════════════════════════════════════════════════
            //  日志
            // ══════════════════════════════════════════════════
            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

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
                        text: "同步根: " + DriveRoot
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

            Item { Layout.fillHeight: true }

            // ── 底部按钮 ──
            Button {
                Layout.fillWidth: true
                Layout.preferredHeight: 34
                text: "关闭"
                onClicked: page.close()
                contentItem: Label {
                    text: "关闭"
                    color: Theme.textHi
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.pixelSize: Theme.fsBody
                }
                background: Rectangle {
                    radius: Theme.radius
                    color: parent.hovered ? Theme.bgHover : Theme.bgCard
                    border.color: Theme.borderHi
                }
            }
        }
    }
}
