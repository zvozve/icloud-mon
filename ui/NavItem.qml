// ======================================================================
//  NavItem.qml  -  导航栏的一行 (仿 UWP NavigationView 的 NavigationViewItem)
//
//  选中态: 左侧 3px 强调色指示条 + 高亮底
//  折叠态: 只留图标并居中, 文字淡出, 悬停用 ToolTip 补文字
// ======================================================================
import QtQuick 2.15
import QtQuick.Controls 2.15

Rectangle {
    id: item

    property string text: ""
    property string icon: "home"
    property bool selected: false
    property bool collapsed: false
    signal activated()

    height: Theme.navItemH
    color: selected ? Theme.navActive
                    : (hover.containsMouse ? Theme.navHover : "transparent")
    Behavior on color { ColorAnimation { duration: 120 } }

    // ── 选中指示条 ──
    Rectangle {
        visible: item.selected
        width: 3
        height: item.height * 0.6
        anchors.verticalCenter: parent.verticalCenter
        x: 0
        radius: 1.5
        color: Theme.accent
    }

    // ── 图标 ──
    // 折叠时居中, 展开时靠左。x 用动画过渡, 和 UWP 的收展观感一致。
    NavIcon {
        id: ic
        kind: item.icon
        color: item.selected ? Theme.accent
                             : (hover.containsMouse ? Theme.textHi
                                                    : Theme.textMid)
        anchors.verticalCenter: parent.verticalCenter
        x: item.collapsed ? (item.width - width) / 2 : 16
        Behavior on x {
            NumberAnimation { duration: 150; easing.type: Easing.OutCubic }
        }
    }

    // ── 文字 ──
    Label {
        id: label
        anchors.verticalCenter: parent.verticalCenter
        x: 44
        text: item.text
        color: item.selected ? Theme.textHi : Theme.textMid
        font.pixelSize: Theme.fsBody
        font.bold: item.selected
        opacity: item.collapsed ? 0 : 1
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 120 } }
    }

    MouseArea {
        id: hover
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: item.activated()
    }

    // 折叠后看不到文字, 用 ToolTip 补上
    ToolTip.visible: item.collapsed && hover.containsMouse
    ToolTip.text: item.text
    ToolTip.delay: 500
}
