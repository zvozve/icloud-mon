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

    // ── 云盘: 三个交叠的圆, 构成云朵轮廓 ──
    Item {
        anchors.fill: parent
        visible: root.kind === "drive"

        readonly property real r: root.width * 0.42

        Rectangle {
            x: 0
            y: parent.height - height
            width: parent.r; height: parent.r; radius: parent.r / 2
            color: root.color
        }
        Rectangle {
            x: parent.width * 0.22
            y: parent.height * 0.10
            width: parent.r * 1.25; height: parent.r * 1.25
            radius: height / 2
            color: root.color
        }
        Rectangle {
            x: parent.width - width
            y: parent.height * 0.42
            width: parent.r * 0.92; height: parent.r * 0.92
            radius: height / 2
            color: root.color
        }
    }

    // ── 相册: 圆角矩形外框 + 右上一个小实心圆 ──
    Item {
        anchors.fill: parent
        visible: root.kind === "photos"

        Rectangle {
            anchors.fill: parent
            radius: 2
            color: "transparent"
            border.color: root.color
            border.width: root.lw
        }
        Rectangle {
            x: parent.width * 0.62
            y: parent.height * 0.18
            width: parent.width * 0.2; height: width
            radius: width / 2
            color: root.color
        }
    }

    // ── 密码: 锁 —— 上方拱形 + 下方锁体 + 中间锁孔 ──
    Item {
        anchors.fill: parent
        visible: root.kind === "passwords"

        // 拱形(锁梁): 只用圆角矩形近似, 下半截被锁体盖住
        Rectangle {
            x: parent.width * 0.28
            y: 0
            width: parent.width * 0.44
            height: parent.height * 0.55
            radius: width / 2
            color: "transparent"
            border.color: root.color
            border.width: root.lw
        }
        // 锁体
        Rectangle {
            x: 0
            y: parent.height * 0.45
            width: parent.width
            height: parent.height * 0.55
            radius: 2
            color: root.color
        }
        // 锁孔(用导航底色挖出来)
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            y: parent.height * 0.62
            width: Math.max(2, parent.width * 0.16)
            height: parent.height * 0.26
            radius: width / 2
            color: Theme.navBg
        }
    }

    // ── 书签: 矩形 + 底部三角缺口 ──
    Item {
        anchors.fill: parent
        visible: root.kind === "bookmarks"

        // 主体
        Rectangle {
            x: parent.width * 0.14
            y: 0
            width: parent.width * 0.72
            height: parent.height
            radius: 2
            color: root.color
        }
        // 底部缺口: 用一个旋转 45 度的底色方块切出一个 V
        Rectangle {
            x: parent.width * 0.5 - width / 2
            y: parent.height * 0.78
            width: parent.width * 0.52
            height: width
            rotation: 45
            color: Theme.navBg
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
