// ======================================================================
//  NavIcon.qml  -  导航栏图标
//
//  用图元画, 不依赖 Segoe MDL2 Assets / Segoe Fluent Icons 等图标字体。
//  原因: 本会话的窗口从不渲染(见 main.cpp 的 [GPU] 探针), 无法视觉验证,
//  而字体码点记错就会显示成方框; 几何图元不存在这种失败模式。
//  本机两个图标字体都在, 想换回原生字形只需改这里的实现。
//
//  kind: "menu" | "home" | "status" | "settings"
// ======================================================================
import QtQuick 2.15

Item {
    id: root

    property string kind: "home"
    property color color: Theme.textMid
    // 线宽, 跟随图标尺寸等比缩放
    readonly property real lw: Math.max(1.5, width / 9)

    implicitWidth: Theme.navIconSize
    implicitHeight: Theme.navIconSize
    width: implicitWidth
    height: implicitHeight

    // ── 汉堡菜单: 三条横线 ──
    Column {
        anchors.centerIn: parent
        visible: root.kind === "menu"
        spacing: Math.round(root.height / 6)

        Repeater {
            model: 3
            delegate: Rectangle {
                width: root.width
                height: root.lw
                radius: height / 2
                color: root.color
            }
        }
    }

    // ── 首页: 2x2 概览方块 ──
    Grid {
        anchors.centerIn: parent
        visible: root.kind === "home"
        columns: 2
        spacing: Math.max(2, Math.round(root.width / 8))

        readonly property real cell: (root.width - spacing) / 2

        Repeater {
            model: 4
            delegate: Rectangle {
                width: parent.cell
                height: parent.cell
                radius: 1.5
                color: root.color
            }
        }
    }

    // ── 状态: 三根柱状图 ──
    // 不用 Row/Column 定位再叠 anchors —— 定位器内的子项禁止用 anchors,
    // 直接给 x/y 最稳。
    Item {
        anchors.fill: parent
        visible: root.kind === "status"

        readonly property real bw: (width - 4) / 3
        readonly property real gap: 2

        Rectangle {
            x: 0
            width: parent.bw
            height: parent.height * 0.45
            y: parent.height - height
            radius: 1.5
            color: root.color
        }
        Rectangle {
            x: parent.bw + parent.gap
            width: parent.bw
            height: parent.height
            y: 0
            radius: 1.5
            color: root.color
        }
        Rectangle {
            x: (parent.bw + parent.gap) * 2
            width: parent.bw
            height: parent.height * 0.7
            y: parent.height - height
            radius: 1.5
            color: root.color
        }
    }

    // ── 设置: 三条带旋钮的滑条 ──
    Item {
        id: sliders
        anchors.fill: parent
        visible: root.kind === "settings"

        // 每个旋钮在各自那行上的相对位置
        readonly property var knobAt: [0.18, 0.68, 0.42]
        readonly property real rowH: height / 3
        readonly property real knob: Math.max(4, root.width / 3.2)

        Repeater {
            model: 3

            delegate: Item {
                required property int index

                x: 0
                y: index * sliders.rowH
                width: sliders.width
                height: sliders.rowH

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    x: 0
                    width: parent.width
                    height: root.lw
                    radius: height / 2
                    color: root.color
                }

                // 旋钮: 用导航栏底色挖空线条, 比画实心块更像 UWP 的字形
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    x: (parent.width - width) * sliders.knobAt[index]
                    width: sliders.knob
                    height: width
                    radius: width / 2
                    color: Theme.navBg
                    border.color: root.color
                    border.width: root.lw
                }
            }
        }
    }
}
