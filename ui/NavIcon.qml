// ======================================================================
//  NavIcon.qml  -  导航栏图标
//
//  用图元画, 不依赖 Segoe MDL2 Assets / Segoe Fluent Icons 等图标字体。
//  原因: 本会话的窗口从不渲染(见 main.cpp 的 [GPU] 探针), 无法视觉验证,
//  而字体码点记错就会显示成方框; 几何图元不存在这种失败模式。
//  本机两个图标字体都在, 想换回原生字形只需改这里的实现。
//
//  kind: "menu" | "home" | "status" | "settings"
//        | "drive" | "photos" | "passwords" | "bookmarks"
//        | "folder" | "file" | "upload" | "download" | "trash"
// ======================================================================
import QtQuick 2.15

Item {
    id: root

    property string kind: "home"
    property color color: Theme.textMid
    // 挖空用的底色。
    // 密码锁孔、书签缺口、设置旋钮这几个形状是靠"再画一块底色"挖出来的,
    // 所以这个颜色必须等于当前背景色, 否则会露出一块突兀的补丁。
    // 原来硬编码 Theme.navBg —— 只在导航栏里成立, 拿到命令栏(bgPanel)
    // 或卡片上就挖错色。改成属性, 由使用处传对。
    property color maskColor: Theme.navBg
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
            color: root.maskColor
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
            color: root.maskColor
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

                // 旋钮: 用 nav 底色挖空线条, 比画实心块更像 UWP 的字形
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    x: (parent.width - width) * sliders.knobAt[index]
                    width: sliders.knob
                    height: width
                    radius: width / 2
                    color: root.maskColor
                    border.color: root.color
                    border.width: root.lw
                }
            }
        }
    }

    // ── 文件夹: 后标签 + 前主体 ──
    // 替代原来的 📂 emoji。emoji 是彩色位图, 混在单色矢量图标里最突兀,
    // 而且颜色不受主题控制(改 Theme 也不会变), 在深色界面上尤其跳。
    Item {
        anchors.fill: parent
        visible: root.kind === "folder"

        // 后面的标签页
        Rectangle {
            x: 0
            y: parent.height * 0.16
            width: parent.width * 0.44
            height: parent.height * 0.18
            radius: 1.5
            color: root.color
        }
        // 前面的主体
        Rectangle {
            x: 0
            y: parent.height * 0.28
            width: parent.width
            height: parent.height * 0.62
            radius: 2
            color: root.color
        }
    }

    // ── 文件: 描边页 + 三条内容线 ──
    // 用描边而不是实心, 这样不需要挖空, 放哪个底色上都成立。
    Item {
        anchors.fill: parent
        visible: root.kind === "file"

        Rectangle {
            x: parent.width * 0.2
            y: 0
            width: parent.width * 0.6
            height: parent.height
            radius: 1.5
            color: "transparent"
            border.color: root.color
            border.width: root.lw
        }
        // 三条内容线: 第 3 条短一些, 更像文档
        Rectangle {
            x: parent.width * 0.3
            y: parent.height * 0.28
            width: parent.width * 0.4
            height: root.lw * 0.8
            radius: height / 2
            color: root.color
        }
        Rectangle {
            x: parent.width * 0.3
            y: parent.height * 0.5
            width: parent.width * 0.4
            height: root.lw * 0.8
            radius: height / 2
            color: root.color
        }
        Rectangle {
            x: parent.width * 0.3
            y: parent.height * 0.72
            width: parent.width * 0.24
            height: root.lw * 0.8
            radius: height / 2
            color: root.color
        }
    }

    // ── 上传: 向上箭头 + 底部托盘线 ──
    Item {
        anchors.fill: parent
        visible: root.kind === "upload"

        // 箭杆
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            y: parent.height * 0.3
            width: Math.max(1.5, root.lw * 0.9)
            height: parent.height * 0.44
            radius: width / 2
            color: root.color
        }
        // 箭头: 两根 45 度斜杆拼成 ^ (不用三角形, 斜杆在细尺寸下更清楚)
        Rectangle {
            x: parent.width * 0.5 - width + root.lw * 0.5
            y: parent.height * 0.28
            width: parent.width * 0.3
            height: Math.max(1.5, root.lw * 0.9)
            radius: height / 2
            rotation: -45
            transformOrigin: Item.Right
            color: root.color
        }
        Rectangle {
            x: parent.width * 0.5 + width - root.lw * 0.5
            y: parent.height * 0.28
            width: parent.width * 0.3
            height: Math.max(1.5, root.lw * 0.9)
            radius: height / 2
            rotation: 45
            transformOrigin: Item.Left
            color: root.color
        }
        // 托盘
        Rectangle {
            x: parent.width * 0.12
            y: parent.height * 0.82
            width: parent.width * 0.76
            height: Math.max(1.5, root.lw * 0.9)
            radius: height / 2
            color: root.color
        }
    }

    // ── 下载: 向下箭头 + 底部托盘线 ──
    Item {
        anchors.fill: parent
        visible: root.kind === "download"

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            y: parent.height * 0.24
            width: Math.max(1.5, root.lw * 0.9)
            height: parent.height * 0.44
            radius: width / 2
            color: root.color
        }
        Rectangle {
            x: parent.width * 0.5 - width + root.lw * 0.5
            y: parent.height * 0.56
            width: parent.width * 0.3
            height: Math.max(1.5, root.lw * 0.9)
            radius: height / 2
            rotation: 45
            transformOrigin: Item.Right
            color: root.color
        }
        Rectangle {
            x: parent.width * 0.5 + width - root.lw * 0.5
            y: parent.height * 0.56
            width: parent.width * 0.3
            height: Math.max(1.5, root.lw * 0.9)
            radius: height / 2
            rotation: -45
            transformOrigin: Item.Left
            color: root.color
        }
        Rectangle {
            x: parent.width * 0.12
            y: parent.height * 0.82
            width: parent.width * 0.76
            height: Math.max(1.5, root.lw * 0.9)
            radius: height / 2
            color: root.color
        }
    }

    // ── 回收站: 桶盖 + 提手 + 桶身 ──
    Item {
        anchors.fill: parent
        visible: root.kind === "trash"

        // 提手
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 0
            width: parent.width * 0.26
            height: parent.height * 0.12
            radius: 1
            color: root.color
        }
        // 桶盖
        Rectangle {
            x: 0
            y: parent.height * 0.14
            width: parent.width
            height: Math.max(1.5, root.lw)
            radius: height / 2
            color: root.color
        }
        // 桶身
        Rectangle {
            x: parent.width * 0.16
            y: parent.height * 0.26
            width: parent.width * 0.68
            height: parent.height * 0.74
            radius: 1.5
            color: "transparent"
            border.color: root.color
            border.width: root.lw
        }
    }
}
