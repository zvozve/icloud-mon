// ======================================================================
//  Main.qml  -  主界面（仿 UWP NavigationView）
//
//  布局:
//    ┌──────────┬──────────────────────────────────────┐
//    │ ☰ 标题   │  命令栏: 状态点/版本  iCloud 开关 …  │
//    │ ──────── ├──────────────────────────────────────┤
//    │ ⌗ 首页   │                                      │
//    │ ◫ 状态   │      页面内容 (StackLayout 切页)      │
//    │          │                                      │
//    │ ⚙ 设置   │                                      │
//    └──────────┴──────────────────────────────────────┘
//
//  改动要点(相对旧版):
//    - 去掉 ApplicationWindow.header, 导航栏改为**通高**左侧栏
//      (UWP 的 NavigationView 就是这么占满整个窗口高度的)
//    - 设置从"滑出覆盖层"改成**独立页面**, 由导航切页
//    - 顶栏的设置按钮随之去掉(设置已是导航项), 其余命令保留
// ======================================================================
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

ApplicationWindow {
    id: win
    width: 1080
    height: 820
    minimumWidth: 820
    minimumHeight: 600
    visible: true
    title: Bridge.stateName === "未知" ? "iCloud 状态监控"
                                       : "iCloud 状态监控 - " + Bridge.stateName
    color: Theme.bgRoot

    // 导航栏是否展开(仅图标 <-> 图标+文字)
    property bool navExpanded: true
    // 当前页: 0=首页 1=状态 2=设置
    property int pageIndex: 0

    // ── Toast 状态 ──
    property string toastText: ""
    property string toastKind: "info"

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
    //  整体: 左侧导航 + 右侧内容
    // ══════════════════════════════════════════════════════════════
    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ══════════════════════════════════════════════════════════
        //  导航栏
        // ══════════════════════════════════════════════════════════
        Rectangle {
            id: nav
            Layout.fillHeight: true
            Layout.preferredWidth: win.navExpanded ? Theme.navW : Theme.navWMin
            color: Theme.navBg

            // 宽度变化做动画, 收展不突兀
            Behavior on Layout.preferredWidth {
                NumberAnimation { duration: 150; easing.type: Easing.OutCubic }
            }

            // 右侧分隔线
            Rectangle {
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: Theme.border
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // ── 顶部: 汉堡按钮 + 标题 ──
                Rectangle {
                    Layout.fillWidth: true
                    height: 48
                    color: "transparent"

                    MouseArea {
                        id: menuBtn
                        width: 48
                        height: 48
                        anchors.left: parent.left
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: win.navExpanded = !win.navExpanded

                        NavIcon {
                            anchors.centerIn: parent
                            kind: "menu"
                            color: menuBtn.containsMouse ? Theme.textHi
                                                         : Theme.textMid
                        }
                    }

                    Label {
                        anchors.left: parent.left
                        anchors.leftMargin: 48
                        anchors.verticalCenter: parent.verticalCenter
                        text: "iCloud 监控"
                        color: Theme.textHi
                        font.pixelSize: Theme.fsBody
                        font.bold: true
                        opacity: win.navExpanded ? 1 : 0
                        visible: opacity > 0
                        Behavior on opacity { NumberAnimation { duration: 120 } }
                    }
                }

                // ── 导航项 ──
                NavItem {
                    Layout.fillWidth: true
                    text: "首页"
                    icon: "home"
                    collapsed: !win.navExpanded
                    selected: win.pageIndex === 0
                    onActivated: win.pageIndex = 0
                }

                NavItem {
                    Layout.fillWidth: true
                    text: "状态"
                    icon: "status"
                    collapsed: !win.navExpanded
                    selected: win.pageIndex === 1
                    onActivated: win.pageIndex = 1
                }

                // 把设置推到底部 —— 这是 UWP NavigationView 的惯例:
                // 设置固定在最下方, 和业务导航项分开。
                Item { Layout.fillHeight: true }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    Layout.rightMargin: 8
                    height: 1
                    color: Theme.border
                }

                NavItem {
                    Layout.fillWidth: true
                    text: "设置"
                    icon: "settings"
                    collapsed: !win.navExpanded
                    selected: win.pageIndex === 2
                    onActivated: win.pageIndex = 2
                }

                Item { height: Theme.sp }
            }
        }

        // ══════════════════════════════════════════════════════════
        //  右侧: 命令栏 + 页面
        // ══════════════════════════════════════════════════════════
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ── 命令栏 ──
            Rectangle {
                Layout.fillWidth: true
                height: 48
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

                    // 状态指示灯 + 状态名
                    Rectangle {
                        width: 10; height: 10; radius: 5
                        color: Theme.stateColor(Bridge.stateName)
                        Behavior on color { ColorAnimation { duration: 300 } }
                    }

                    Label {
                        text: Bridge.stateName
                        color: Bridge.dbOk ? Theme.stateColor(Bridge.stateName)
                                           : Theme.err
                        font.pixelSize: Theme.fsTitle
                        font.bold: true
                    }

                    Label {
                        text: Bridge.dbOk ? ("v" + Bridge.clientVersion)
                                          : "数据库不可读"
                        color: Theme.textLo
                        font.pixelSize: Theme.fsSmall
                    }

                    Item { Layout.fillWidth: true }

                    // ── iCloud 开关 ──
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
                            // 开关状态完全由后端进程事实驱动:
                            // 用户点击 -> 调 restart 而不是直接改 checked,
                            // 避免出现"界面显示开着但实际没运行"的假状态。
                            checked: Bridge.icloudRunning
                            enabled: !Bridge.busyRestarting
                            onToggled: {
                                if (checked !== Bridge.icloudRunning) {
                                    Bridge.restartIcloud()
                                    // 立刻弹回, 等后端确认后再由
                                    // onIcloudStateChanged 更新
                                    checked = Bridge.icloudRunning
                                }
                            }

                            contentItem: Rectangle {
                                implicitWidth: 42
                                implicitHeight: 20
                                radius: 10
                                color: icloudSwitch.checked ? Theme.ok : Theme.bgInput
                                border.color: icloudSwitch.checked ? Theme.ok
                                                                  : Theme.borderHi
                                opacity: icloudSwitch.enabled ? 1.0 : 0.5
                                Behavior on color { ColorAnimation { duration: 160 } }

                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    x: icloudSwitch.checked
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

                    // ── 打开 iCloud 目录 ──
                    Button {
                        id: openDriveBtn
                        width: 34; height: 30
                        onClicked: Bridge.openDriveRoot()
                        ToolTip.visible: hovered
                        ToolTip.text: "打开 iCloud 目录\n" + Bridge.driveRoot
                        ToolTip.delay: 600

                        contentItem: Text {
                            text: "📂"
                            color: openDriveBtn.hovered ? Theme.infoHi : Theme.textMid
                            font.pixelSize: 15
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            radius: Theme.radius
                            color: openDriveBtn.hovered ? Theme.bgHover : "transparent"
                            border.color: openDriveBtn.hovered ? Theme.borderHi
                                                               : "transparent"
                        }
                    }
                }
            }

            // ── 页面容器 ──
            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: win.pageIndex

                OverviewPage {}
                StatusPage {}
                SettingsPage {}
            }
        }
    }

    // ══════════════════════════════════════════════════════════════
    //  Toast (右下角临时提示)
    // ══════════════════════════════════════════════════════════════
    Rectangle {
        id: toast
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spL

        // 宽度用**固定值**, 不要去测量内部 RowLayout 的 implicitWidth。
        // 这里踩过一个绑定循环: toastRow 是 anchors.fill: parent,
        // 它的宽度来自 toast.width; 而 RowLayout 内含 wrapMode 的 Label,
        // 其 implicitWidth 又依赖被分配的宽度 -> 互相引用, 运行时报
        // "QML Rectangle: Binding loop detected for property width"。
        readonly property int toastW: 360
        width: toastText.length > 0 ? toastW : 0
        // 高度 = 内容高 + 上下各 Theme.sp(与 toastRow 的 anchors.margins 对应)
        height: toastText.length > 0 ? toastRow.implicitHeight + Theme.sp * 2 : 0
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
        // Behavior 没有 when 属性(只有 enabled)
        Behavior on width {
            enabled: win.toastText.length > 0
            NumberAnimation { duration: 180 }
        }

        RowLayout {
            id: toastRow
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
                Layout.maximumWidth: 320
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: win.toastText = ""
        }
    }
}
