// ======================================================================
//  StatusCard.qml  -  当前状态大卡片
//  显示: 状态名 + 详情 + 同步游标 + 最近错误
// ======================================================================
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root
    implicitHeight: 96
    color: Theme.bgPanel
    radius: Theme.radius
    border.width: 1

    // Rectangle 没有 borderColor 这个属性, 边框在 border.color 分组里,
    // 而分组属性不能直接挂 Behavior/Animation。
    // 办法: 起一个中介色, 动画它, 再喂给 border.color。
    property color edgeColor: Theme.border
    border.color: Bridge.anomalous ? edgeColor : Theme.border

    // 异常时边框呼吸, 不用闪, 减少视觉噪音。
    //
    // 说明: Animation 类没有 when 属性(只有 running/paused/loops),
    // 之前我按 when 写过一版是错的, 已在日志里报
    // "Cannot assign to non-existent property when"。
    // running 绑定本来就是标准写法, 保持原样。
    SequentialAnimation on edgeColor {
        running: Bridge.anomalous
        loops: Animation.Infinite
        ColorAnimation { to: Theme.err; duration: 900 }
        ColorAnimation { to: Theme.border; duration: 900 }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.spL
        spacing: Theme.spL

        // ── 状态圆点 + 名称 ──
        ColumnLayout {
            spacing: 2
            Layout.alignment: Qt.AlignVCenter

            RowLayout {
                spacing: 8
                Rectangle {
                    width: 12; height: 12; radius: 6
                    color: Theme.stateColor(Bridge.stateName)
                    Behavior on color { ColorAnimation { duration: 300 } }
                }
                Label {
                    text: Bridge.stateName
                    color: Bridge.dbOk ? Theme.stateColor(Bridge.stateName)
                                       : Theme.err
                    font.pixelSize: Theme.fsHuge
                    font.bold: true
                }
            }

            Label {
                text: Bridge.stateDetail
                color: Theme.textMid
                font.pixelSize: Theme.fsBody
            }
        }

        Rectangle {
            width: 1; Layout.fillHeight: true
            color: Theme.border
        }

        // ── 游标 / 错误 / 进程 ──
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            Layout.alignment: Qt.AlignVCenter

            RowLayout {
                spacing: 6
                Label {
                    text: Bridge.inSync ? "✓" : "⟳"
                    color: Bridge.inSync ? Theme.ok : Theme.warn
                    font.pixelSize: Theme.fsBody
                    font.bold: true
                }
                Label {
                    text: Bridge.rankText
                    color: Theme.textLo
                    font.pixelSize: Theme.fsSmall
                    font.family: "Consolas, monospace"
                }
            }

            Label {
                text: Bridge.lastError
                visible: text !== ""
                color: Theme.err
                font.pixelSize: Theme.fsSmall
            }

            Label {
                text: Bridge.icloudProcesses
                visible: text !== ""
                color: Theme.textLo
                font.pixelSize: Theme.fsTiny
                font.family: "Consolas, monospace"
            }
        }

        // ── 刷新按钮 ──
        ColumnLayout {
            spacing: 4
            Layout.alignment: Qt.AlignVCenter

            Button {
                id: refreshBtn
                width: 74; height: 28
                onClicked: Bridge.refreshNow()
                ToolTip.visible: hovered
                ToolTip.text: "立即刷新 (每 " + Bridge.refreshSec + " 秒自动)"
                ToolTip.delay: 500

                contentItem: Label {
                    text: "刷新"
                    color: refreshBtn.hovered ? Theme.textHi : Theme.textMid
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.pixelSize: Theme.fsSmall
                }
                background: Rectangle {
                    radius: Theme.radius
                    color: refreshBtn.hovered ? Theme.bgHover : Theme.bgCard
                    border.color: Theme.borderHi
                }
            }

            Label {
                // 在 ColumnLayout 里不能用 anchors 居中(会与 Layout 打架),
                // 要用 Layout.alignment
                Layout.alignment: Qt.AlignHCenter
                text: "更新 " + Bridge.timestamp
                color: Theme.textLo
                font.pixelSize: Theme.fsTiny
            }
        }
    }
}
