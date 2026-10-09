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
    property string icon: ""

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

            Label {
                text: root.icon
                color: root.accent
                font.pixelSize: Theme.fsTitle
                font.bold: true
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
                    text: "待处理"
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

            ColumnLayout {
                spacing: 1
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
