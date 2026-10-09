// ======================================================================
//  Main.qml  -  主界面
//
//  布局:
//    顶栏  : 标题 + iCloud 运行开关 + 设置按钮
//    横幅  : 异常告警 (仅异常时出现)
//    状态卡: 当前状态大标题 + 同步游标
//    上/下载: 两个队列面板 (完成数/总数)
//    云端分布: pcs_state 分布条
//    待处理项: 明细列表
// ======================================================================
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

ApplicationWindow {
    id: win
    width: 1080
    height: 820
    minimumWidth: 900
    minimumHeight: 640
    visible: true
    title: Bridge.stateName === "未知" ? "iCloud 状态监控"
                                       : "iCloud 状态监控 - " + Bridge.stateName
    color: Theme.bgRoot

    // ── 状态栏提示 ──
    property string toastText: ""
    property string toastKind: "info"
    property int toastTimer: 0

    function showToast(kind, text) {
        toastKind = kind
        toastText = text
        toastTimer.restart()
    }

    Connections {
        target: Bridge
        function onNotify(kind, text) {
            win.showToast(kind, text)
        }
        function onIcloudStateChanged() {
            // 重启后进程状态变了, 立刻刷一次界面
            win.ipcTimer.restart()
        }
    }

    Timer {
        id: toastTimer
        interval: 4000
        onTriggered: win.toastText = ""
    }

    // 进程状态变化后延迟一点再采同步数据, 等 iCloud 起来
    Timer {
        id: ipcTimer
        interval: 4000
        onTriggered: Bridge.refreshNow()
    }

    // ══════════════════════════════════════════════════════════════
    //  顶栏
    // ══════════════════════════════════════════════════════════════
    header: Rectangle {
        height: 60
        color: Theme.bgPanel

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.border
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spL
            anchors.rightMargin: Theme.spL
            spacing: Theme.sp

            // 状态指示灯
            Rectangle {
                width: 10; height: 10; radius: 5
                color: Theme.stateColor(Bridge.stateName)
                Behavior on color { ColorAnimation { duration: 300 } }
            }

            Label {
                text: "iCloud 同步监控"
                color: Theme.textHi
                font.pixelSize: Theme.fsTitle
                font.bold: true
            }

            Label {
                text: Bridge.dbOk ? ("v" + Bridge.clientVersion) : "数据库不可读"
                color: Theme.textLo
                font.pixelSize: Theme.fsSmall
            }

            Item { Layout.fillWidth: true }

            // ── iCloud 开关 (核心操作) ──
            RowLayout {
                spacing: 6

                Label {
                    text: Bridge.icloudRunning ? "iCloud 运行中" : "iCloud 已停止"
                    color: Bridge.icloudRunning ? Theme.ok : Theme.err
                    font.pixelSize: Theme.fsSmall
                    font.bold: true
                }

                Switch {
                    id: icloudSwitch
                    // 开关的 checked 状态完全由后端进程事实驱动,
                    // 用户点击 -> 调 restart 而不是直接改 checked,
                    // 避免出现"界面显示开着但实际没运行"的假状态。
                    checked: Bridge.icloudRunning
                    enabled: !Bridge.busyRestarting
                    onToggled: {
                        if (checked !== Bridge.icloudRunning) {
                            Bridge.restartIcloud()
                            // 立刻弹回, 等后端确认后再由 onIcloudStateChanged 更新
                            checked = Bridge.icloudRunning
                        }
                    }

                    contentItem: Rectangle {
                        implicitWidth: 42
                        implicitHeight: 20
                        radius: 10
                        color: icloudSwitch.checked ? Theme.ok : Theme.bgInput
                        border.color: icloudSwitch.checked ? Theme.ok : Theme.borderHi
                        opacity: icloudSwitch.enabled ? 1.0 : 0.5

                        Behavior on color { ColorAnimation { duration: 160 } }

                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            x: icloudSwitch.checked
                               ? parent.width - width - 2 : 2
                            width: 16; height: 16; radius: 8
                            color: "#ffffff"
                            Behavior on x {
                                NumberAnimation { duration: 160; easing.type: Easing.OutCubic }
                            }
                        }
                    }
                }

                Button {
                    id: restartBtn
                    text: Bridge.busyRestarting ? "重启中..." : "重启"
                    enabled: !Bridge.busyRestarting
                    onClicked: Bridge.restartIcloud()
                    ToolTip.visible: hovered
                    ToolTip.text: "结束全部 iCloud 进程后重新启动"
                    ToolTip.delay: 600

                    contentItem: Label {
                        text: restartBtn.text
                        color: restartBtn.enabled ? Theme.textHi : Theme.textLo
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.pixelSize: Theme.fsSmall
                    }
                    background: Rectangle {
                        implicitWidth: 72
                        implicitHeight: 30
                        radius: Theme.radius
                        color: restartBtn.hovered ? Theme.bgHover : Theme.bgCard
                        border.color: Theme.borderHi
                    }
                }
            }

            // ── 设置按钮 ──
            Button {
                id: settingsBtn
                width: 34; height: 30
                onClicked: settingsPage.open()
                ToolTip.visible: hovered
                ToolTip.text: "设置"
                ToolTip.delay: 600

                contentItem: Text {
                    text: "⚙"                       // 齿轮
                    color: settingsBtn.hovered ? Theme.textHi : Theme.textMid
                    font.pixelSize: 17
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: Theme.radius
                    color: settingsBtn.hovered ? Theme.bgHover : "transparent"
                    border.color: settingsBtn.hovered ? Theme.borderHi : "transparent"
                }
            }
        }
    }

    // ══════════════════════════════════════════════════════════════
    //  主体
    // ══════════════════════════════════════════════════════════════
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spL
        spacing: Theme.sp

        // ── 异常横幅 ──
        AlertBanner {
            id: banner
            Layout.fillWidth: true
            visible: Bridge.anomalous
            // 折叠动画: 出现/隐藏都有过渡, 不做突兀的跳变
            Layout.preferredHeight: visible ? implicitHeight : 0
            Behavior on Layout.preferredHeight {
                NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
            }
        }

        // ── 状态卡 ──
        StatusCard {
            Layout.fillWidth: true
        }

        // ── 上/下载 队列 ──
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 172
            spacing: Theme.sp

            QueuePanel {
                Layout.fillWidth: true
                Layout.fillHeight: true
                title: "上传"
                accent: Theme.up
                done: Bridge.upDone
                total: Bridge.upTotal
                pendingCount: Bridge.upCount
                pendingSize: Bridge.upSize
                busy: Bridge.upBusy
                icon: "↑"
            }

            QueuePanel {
                Layout.fillWidth: true
                Layout.fillHeight: true
                title: "下载"
                accent: Theme.down
                done: Bridge.dlDone
                total: Bridge.dlTotal
                pendingCount: Bridge.dlCount
                pendingSize: Bridge.dlSize
                extraText: Bridge.dlPending > 0
                            ? ("取回队列 " + Bridge.dlPending + " 条") : ""
                busy: Bridge.dlBusy
                icon: "↓"
            }
        }

        // ── 云端分布 + 待处理项 ──
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.sp

            PcsPanel {
                Layout.fillWidth: true
                Layout.fillHeight: true
            }

            // 待处理项明细
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.bgPanel
                radius: Theme.radius
                border.color: Theme.border
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.sp
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: "待处理项"
                            color: Theme.textHi
                            font.pixelSize: Theme.fsBody
                            font.bold: true
                        }
                        Item { Layout.fillWidth: true }
                        Label {
                            text: Bridge.pendingItems.count + " 文件 + "
                                  + (Bridge.localPendings.count) + " 本地未提交"
                            color: Theme.textLo
                            font.pixelSize: Theme.fsTiny
                        }
                    }

                    // 本地未提交项 (卡死源头, 优先展示)
                    Repeater {
                        model: Bridge.localPendings
                        delegate: Rectangle {
                            required property string name
                            required property string stateText
                            width: ListView.view.width
                            height: 26
                            color: Theme.bgCard
                            radius: 3

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 6
                                anchors.rightMargin: 6
                                spacing: 6

                                Rectangle {
                                    width: 5; height: 5; radius: 2.5
                                    color: Theme.warn
                                }
                                Label {
                                    text: name
                                    color: Theme.textMid
                                    font.pixelSize: Theme.fsSmall
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }
                                Label {
                                    text: stateText
                                    color: Theme.warn
                                    font.pixelSize: Theme.fsTiny
                                }
                            }

                            ToolTip.visible: nameArea.containsMouse
                            ToolTip.text: name
                            HoverHandler { id: nameArea }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: Theme.border
                        visible: Bridge.localPendings.count > 0
                                && Bridge.pendingItems.count > 0
                    }

                    // 云端待处理项
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: Bridge.pendingItems
                        spacing: 2
                        ScrollBar.vertical: ScrollBar {}

                        delegate: Rectangle {
                            required property string name
                            required property string sizeText
                            required property bool isDir
                            width: ListView.view.width
                            height: 24
                            color: index % 2 === 0 ? "transparent" : Theme.bgPanel

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 6
                                anchors.rightMargin: 6
                                spacing: 6

                                Label {
                                    text: isDir ? "📁" : "📄"
                                    font.pixelSize: Theme.fsSmall
                                }
                                Label {
                                    text: name
                                    color: Theme.textMid
                                    font.pixelSize: Theme.fsSmall
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }
                                Label {
                                    text: sizeText
                                    color: Theme.textLo
                                    font.pixelSize: Theme.fsTiny
                                }
                            }
                        }
                    }

                    // 空状态
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.topMargin: 20
                        text: Bridge.pendingItems.count === 0
                              && Bridge.localPendings.count === 0
                              ? "无待处理项，同步已追平" : ""
                        visible: text !== ""
                        color: Theme.textLo
                        font.pixelSize: Theme.fsSmall
                    }
                }
            }
        }
    }

    // ══════════════════════════════════════════════════════════════
    //  设置页
    // ══════════════════════════════════════════════════════════════
    SettingsPage {
        id: settingsPage
        anchors.fill: parent
        visible: false
        z: 100
    }

    // ══════════════════════════════════════════════════════════════
    //  Toast (右下角临时提示)
    // ══════════════════════════════════════════════════════════════
    Rectangle {
        id: toast
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spL
        width: toastText.length > 0 ? Math.min(420, implicitWidth + Theme.spL * 2) : 0
        height: toastText.length > 0 ? implicitHeight + Theme.sp : 0
        visible: opacity > 0
        opacity: toastText.length > 0 ? 1 : 0
        color: Theme.bgCard
        radius: Theme.radius
        border.color: toastKind === "error" ? Theme.err
                     : toastKind === "warn"  ? Theme.warn
                     : toastKind === "ok"    ? Theme.ok : Theme.infoHi
        border.width: 1
        z: 200

        Behavior on opacity { NumberAnimation { duration: 200 } }
        Behavior on width { NumberAnimation { duration: 180 } }

        RowLayout {
            anchors.fill: parent
            anchors.margins: Theme.sp
            spacing: 8

            Rectangle {
                width: 7; height: 7; radius: 3.5
                color: toastKind === "error" ? Theme.err
                     : toastKind === "warn"  ? Theme.warn
                     : toastKind === "ok"    ? Theme.ok : Theme.info
            }
            Label {
                text: win.toastText
                color: Theme.textHi
                font.pixelSize: Theme.fsSmall
                wrapMode: Text.WordWrap
                Layout.maximumWidth: 380
            }
        }

        // 点击关闭
        MouseArea {
            anchors.fill: parent
            onClicked: win.toastText = ""
        }
    }
}
