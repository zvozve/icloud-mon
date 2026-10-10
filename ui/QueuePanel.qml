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
                text: root.busy || root.total > 0
                      ? (root.done + " / " + root.total)
                      : "空闲"
                color: root.busy ? root.accent : Theme.textLo
                font.pixelSize: Theme.fsTitle
                font.bold: true
                font.family: "Consolas, monospace"
            }
        }

        // ── 进度条 ──
        Rectangle {
            Layout.fillWidth: true
            height: 8
            radius: 4
            color: Theme.bgInput

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

        Item { Layout.fillHeight: true; Layout.preferredHeight: 4 }

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
