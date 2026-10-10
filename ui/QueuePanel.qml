// ======================================================================
//  QueuePanel.qml  -  上传/下载队列面板
//  核心显示: 完成数/总数 + 进度条 + 待处理数量与字节
// ======================================================================
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root

    property string title: ""
    property color accent: Theme.info
    property int done: 0
    property int total: 0
    property int pendingCount: 0
    property string pendingSize: ""
    property string extraText: ""
    property bool busy: false
    // NavIcon 的 kind 名(不是任意字符)。原来这里传的是 "↑" / "↓" / "⌫"
    // 三个文本符号, 和界面其它地方的矢量图标不是一套线条, 大小与粗细也对不上。
    property string icon: ""
    // "待处理"这一栏的标题可换。删除队列要显示"待删除" ——
    // 叫"待处理"会让人看不出是删除。
    property string countLabel: "待处理"

    // ★ Rectangle 默认 implicitHeight 为 0。外层原本写死 200, 去掉之后
    //   必须自己算高度, 否则三个面板会塌成线。
    //   构成: 标题行(约 20) + 进度条 8(或隐藏时的同高占位 8)
    //        + 间隔 4 + 详情行(约 34) + 上下边距(sp×2)
    implicitHeight: Theme.sp * 2 + 20 + 8 + 4 + 34

    // 删除队列没有"进度"概念。
    //
    // 原来它复用 done/total 画进度条, 而删除的 done = 回收站项数、
    // total = 回收站 + 待删除。结果只要回收站非空就恒等于 100%,
    // 于是一根**红色满格进度条**长期挂在界面上(截图实测 3/3)。
    // 红色 + 满格 = 视觉上像"出错/告警", 但真实语义只是
    // "回收站里有 3 项, 没有待删除" —— 这是正常状态。
    // 所以删除队列改用 noProgress: 不画进度条, 标题右侧直接显示
    // "回收站 N 项", 下面的明细行照旧显示"待删除 N 个"。
    property bool noProgress: false

    // 进度比例, total 为 0 时视为 100% (无任务即完成)
    readonly property real ratio: total > 0
        ? Math.min(1.0, done / total)
        : (busy ? 0.0 : 1.0)

    color: Theme.bgPanel
    radius: Theme.radius
    border.width: 1

    // 同StatusCard: border.color 是分组属性不能挂 Behavior,
    // 借中介色做过渡。
    property color edgeColor: Theme.border
    border.color: busy ? accent : edgeColor
    Behavior on edgeColor { ColorAnimation { duration: 240 } }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.sp
        spacing: 6

        // ── 标题行 ──
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            NavIcon {
                kind: root.icon
                color: root.accent
                // 挖空色跟随面板底色, 否则图标上会露出导航栏的颜色
                maskColor: Theme.bgPanel
                Layout.alignment: Qt.AlignVCenter
            }
            Label {
                text: root.title
                color: Theme.textHi
                font.pixelSize: Theme.fsBody
                font.bold: true
            }

            Item { Layout.fillWidth: true }

            // 空闲时不显示 0/0 这种无意义数字
            Label {
                text: root.noProgress
                      ? (root.total > 0 ? (root.total + " 项") : "无记录")
                      : (root.busy || root.total > 0
                         ? (root.done + " / " + root.total)
                         : "空闲")
                color: root.busy ? root.accent : Theme.textLo
                font.pixelSize: Theme.fsTitle
                font.bold: true
                font.family: "Consolas, monospace"
            }
        }

        // ── 进度条 ──
        // noProgress(删除队列)时整条隐藏, 不占高度。
        Rectangle {
            Layout.fillWidth: true
            height: 8
            radius: 4
            color: Theme.bgInput
            visible: !root.noProgress

            Rectangle {
                width: parent.width * root.ratio
                height: parent.height
                radius: parent.radius
                color: root.accent
                opacity: root.busy ? 1.0 : 0.55

                Behavior on width {
                    NumberAnimation { duration: 320; easing.type: Easing.OutCubic }
                }
                Behavior on opacity { NumberAnimation { duration: 240 } }
            }
        }

        // 进度条隐藏时留同高的空占位, 否则三个并排面板里
        // 删除面板的下方内容会比另外两个高一截, 底边对不齐。
        Item {
            Layout.fillWidth: true
            height: 8
            visible: root.noProgress
        }

        // ★ 原来这里是 Item { Layout.fillHeight: true; Layout.preferredHeight: 4 }
        //   作用是把底部"待处理/待传输"压到面板最下端, 中间留白。
        //   在"面板高度写死 200"的前提下这看着合理, 但面板一改成按内容
        //   自撑高, 这个 fillHeight 就会变成"索取剩余空间"的请求 ——
        //   它正是那 130px 空白的来源。现在高度由内容决定, 不需要弹性撑开。
        //   改成固定 4px 的间隔, 内容紧凑排列。
        Item { Layout.preferredHeight: 4; Layout.fillWidth: true }

        // ── 待处理详情 ──
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.sp

            ColumnLayout {
                spacing: 1
                Label {
                    text: root.countLabel
                    color: Theme.textLo
                    font.pixelSize: Theme.fsTiny
                }
                Label {
                    text: root.pendingCount + " 个"
                    color: root.busy ? root.accent : Theme.textLo
                    font.pixelSize: Theme.fsBody
                    font.bold: true
                }
            }

            // 删除没有"字节数"的概念(item_recursive_properties 里也没有
            // needs_delete_size), pendingSize 传空串时整栏隐藏,
            // 而不是留一个空白占位。
            ColumnLayout {
                spacing: 1
                visible: root.pendingSize !== ""
                Label {
                    text: "待传输"
                    color: Theme.textLo
                    font.pixelSize: Theme.fsTiny
                }
                Label {
                    text: root.pendingSize
                    color: root.busy ? root.accent : Theme.textLo
                    font.pixelSize: Theme.fsBody
                    font.bold: true
                }
            }

            Item { Layout.fillWidth: true }

            ColumnLayout {
                spacing: 1
                visible: root.extraText !== ""
                Label {
                    text: "队列"
                    color: Theme.textLo
                    font.pixelSize: Theme.fsTiny
                }
                Label {
                    text: root.extraText
                    color: Theme.info
                    font.pixelSize: Theme.fsSmall
                }
            }
        }
    }
}
