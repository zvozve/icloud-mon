// ======================================================================
//  Toast.qml  -  浮层通知（取代会死锁的 QSystemTrayIcon::showMessage）
//
//  · 独立置顶窗口(Frameless + WindowStaysOnTopHint + Tool), 不在任务栏
//    占坑, 也不抢焦点(WindowDoesNotAcceptFocus); 主窗口最小化时也能弹。
//  · 由 C++ 的 NotificationHub 通过 context 属性 "Notifier" 触发:
//      Notifier.requested(kind, title, body)  ->  show(kind, title, body)
//  · kind 决定左侧色条: error 红 / warn 黄 / ok 绿 / system 蓝 / 其他 灰。
//  · 收到新通知会刷新内容并重置自动隐藏计时, 不会叠一堆。
//  · 纯 Rectangle 绘制, 不用 GraphicalEffects(软件渲染后端对它支持差)。
// ======================================================================
import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls 2.15

Window {
    id: toast
    width: 340
    height: 92
    // 无边框 + 置顶 + 工具窗口(无任务栏项) + 不抢焦点
    flags: Qt.FramelessWindowHint
           | Qt.WindowStaysOnTopHint
           | Qt.Tool
           | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    visible: false
    opacity: 0

    // ── 外部调用入口 ──
    function show(kind, title, body) {
        tKind  = kind
        tTitle = title || ""
        tBody  = body  || ""
        // 右下角停靠(留出任务栏/边距余量)
        x = Screen.width  - width  - 18
        y = Screen.height - height - 18
        visible = true
        showAnim.start()
        hideTimer.restart()
    }

    property string tKind:  "info"
    property string tTitle: ""
    property string tBody:  ""

    // 各类配色(左侧色条 + 标题点)
    readonly property color barColor:
        tKind === "error"  ? "#e5484d" :
        tKind === "warn"   ? "#f5a623" :
        tKind === "ok"     ? "#30a46c" :
        tKind === "system" ? "#3b82f6" : "#6b7280"

    NumberAnimation { id: showAnim; target: toast; property: "opacity"; to: 1; duration: 180 }
    NumberAnimation {
        id: hideAnim; target: toast; property: "opacity"; to: 0; duration: 220
        onStopped: { if (toast.opacity === 0) toast.visible = false }
    }
    // 系统告警停留久一点, 普通操作提示短一点
    Timer {
        id: hideTimer
        interval: tKind === "system" || tKind === "error" ? 6000 : 4000
        onTriggered: hideAnim.start()
    }

    // 点击立即消失
    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: hideAnim.start() }

    Rectangle {
        anchors.fill: parent
        radius: 10
        color: "#ffffff"
        border.color: "#e5e7eb"
        border.width: 1
        clip: true   // 让左侧色条贴合圆角, 不露出方角

        // 左侧色条
        Rectangle {
            width: 5
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            color: toast.barColor
        }

        Column {
            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 14
            anchors.topMargin: 12
            anchors.bottomMargin: 12
            spacing: 4

            Text {
                visible: toast.tTitle !== ""
                text: toast.tTitle
                font { pixelSize: 14; bold: true; family: "Microsoft YaHei UI" }
                color: "#1f2430"
                elide: Text.ElideRight
                width: parent.width
            }
            Text {
                text: toast.tBody
                font { pixelSize: 12; family: "Microsoft YaHei UI" }
                color: "#4b5563"
                wrapMode: Text.Wrap
                width: parent.width
                maximumLineCount: 3
                elide: Text.ElideRight
            }
        }
    }

    // C++ 通知入口
    Connections {
        target: Notifier
        function onRequested(kind, title, body) { toast.show(kind, title, body) }
    }
}
