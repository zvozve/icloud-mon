// ======================================================================
//  Main.qml  -  主界面（仿 UWP NavigationView，6 个页面）
//
//  ┌──────────┬──────────────────────────────────────┐
//  │ ☰ 标题   │  命令栏: 状态点/版本   iCloud 开关 …  │
//  │ ──────── ├──────────────────────────────────────┤
//  │ ⌗ 首页   │                                      │
//  │ ☁ 云盘   │      页面内容（StackLayout 切页）      │
//  │ ▣ 相册   │                                      │
//  │ ⚿ 密码   │                                      │
//  │ ⊞ 书签   │                                      │
//  │ ⚙ 设置   │                                      │
//  └──────────┴──────────────────────────────────────┘
//
//  · 首页固定最前、设置固定最后, 中间四类顺序由 Bridge.navOrder 决定
//    (可在设置页调整, 存 QSettings)
//  · 用 StackLayout 而不是 Loader: StackLayout 的子项顺序固定, 侧边栏换序
//    只影响导航显示, 不用重算索引映射, 简单且不会出错
//  · 默认启动页来自 Bridge.defaultPage, 只在启动时取一次
//    (不做成绑定, 否则用户一改设置当前页就被拽走)
// ======================================================================
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

ApplicationWindow {
    id: win
    width: 1100
    height: 840
    minimumWidth: 860
    minimumHeight: 620
    visible: true
    title: Bridge.stateName === "未知" ? "iCloud 状态监控"
                                       : "iCloud 状态监控 - " + Bridge.stateName
    color: Theme.bgRoot

    // 导航栏是否展开(仅图标 <-> 图标+文字)
    property bool navExpanded: true
    // 当前页面 key: home | drive | photos | passwords | bookmarks | settings
    property string currentKey: "home"

    // 启动时按用户设置的默认启动页定位
    Component.onCompleted: currentKey = Bridge.defaultPage

    // 类别 key -> 图标种类
    function iconFor(k) {
        switch (k) {
        case "drive":     return "drive"
        case "photos":    return "photos"
        case "passwords": return "passwords"
        case "bookmarks": return "bookmarks"
        }
        return "status"
    }

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
            win.ipcTimer.restart()
        }
    }

    Timer {
        id: toastTimer
        interval: 4000
        onTriggered: win.toastText = ""
    }

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
        //  导航栏 (通高)
        // ══════════════════════════════════════════════════════════
        Rectangle {
            id: nav
            Layout.fillHeight: true
            Layout.preferredWidth: win.navExpanded ? Theme.navW : Theme.navWMin
            color: Theme.navBg
            Behavior on Layout.preferredWidth {
                NumberAnimation { duration: 150; easing.type: Easing.OutCubic }
            }

            Rectangle {
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: Theme.border
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // ── 顶部: 汉堡 + 标题 ──
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

                // ── 首页(固定第一) ──
                NavItem {
                    Layout.fillWidth: true
                    text: "首页"
                    icon: "home"
                    collapsed: !win.navExpanded
                    selected: win.currentKey === "home"
                    onActivated: win.currentKey = "home"
                }

                // ── 中间四类(顺序可调) ──
                Repeater {
                    model: Bridge.navOrder

                    delegate: NavItem {
                        required property string modelData
                        Layout.fillWidth: true
                        text: Bridge.categoryName(modelData)
                        icon: win.iconFor(modelData)
                        collapsed: !win.navExpanded
                        selected: win.currentKey === modelData
                        onActivated: win.currentKey = modelData
                    }
                }

                // 设置固定最底 —— UWP NavigationView 的惯例
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
                    selected: win.currentKey === "settings"
                    onActivated: win.currentKey = "settings"
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
                            // 用户点击 -> 调 restart 而不是直接改 checked
                            checked: Bridge.icloudRunning
                            enabled: !Bridge.busyRestarting
                            onToggled: {
                                if (checked !== Bridge.icloudRunning) {
                                    Bridge.restartIcloud()
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
            // 子项顺序固定, 与侧边栏顺序无关(侧边栏顺序只影响导航显示)
            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: {
                    switch (win.currentKey) {
                    case "home":      return 0
                    case "drive":     return 1
                    case "photos":    return 2
                    case "passwords": return 3
                    case "bookmarks": return 4
                    case "settings":  return 5
                    }
                    return 0
                }

                HomePage {}
                DrivePage {}
                CategoryPage { catKey: "photos" }
                CategoryPage { catKey: "passwords" }
                CategoryPage { catKey: "bookmarks" }
                SettingsPage {}
            }
        }
    }

    // ══════════════════════════════════════════════════════════════
    //  Toast
    // ══════════════════════════════════════════════════════════════
    Rectangle {
        id: toast
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spL

        // 宽度用固定值, 不测 RowLayout.implicitWidth(否则绑定循环)
        readonly property int toastW: 360
        width: toastText.length > 0 ? toastW : 0
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
