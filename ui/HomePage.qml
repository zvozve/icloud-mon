// ======================================================================
//  HomePage.qml  -  首页
//
//  只做一件事: 一眼看清四类同步 (云盘/相册/密码/书签) 各自的启用状态。
//  细节全部下沉到各自页面, 这里不放任何队列或明细。
//
//  未启用的类别必须**明显区分**: 整体降透明度 + 灰色描边 + 「未启用」角标。
// ======================================================================
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: page

    Flickable {
        id: body
        anchors.fill: parent
        contentWidth: width
        contentHeight: Math.max(height, col.implicitHeight + Theme.spL * 2)
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ScrollBar.vertical: ScrollBar {
            policy: body.contentHeight > body.height + 1 ? ScrollBar.AsNeeded
                                                        : ScrollBar.AlwaysOff
        }

        ColumnLayout {
            id: col
            width: body.width - Theme.spL * 2
            x: Theme.spL
            y: Theme.spL
            spacing: Theme.sp

            Label {
                text: "四类同步"
                color: Theme.textHi
                font.pixelSize: Theme.fsBig
                font.bold: true
            }

            Label {
                text: "点卡片进入对应页面看细节。未启用的类别已灰化标注。"
                color: Theme.textLo
                font.pixelSize: Theme.fsSmall
            }

            Repeater {
                model: Bridge.categories

                delegate: Rectangle {
                    id: card
                    required property var modelData
                    required property int index

                    // 注意: 不要把这个属性叫 "on" —— 会和 onXxx 信号处理器
                    // 的语法前缀擦边, 叫 isOn 更稳。
                    readonly property bool isOn: modelData.enabled === true

                    Layout.fillWidth: true
                    Layout.preferredHeight: 96
                    color: Theme.bgPanel
                    radius: Theme.radius
                    border.width: 1
                    border.color: isOn ? Theme.border : Theme.borderHi
                    // 未启用整体压暗, 和启用的一眼区分
                    opacity: isOn ? 1.0 : 0.55
                    Behavior on opacity { NumberAnimation { duration: 160 } }

                    // 悬停提亮。放在内容之下, 否则会把文字也染上一层。
                    Rectangle {
                        anchors.fill: parent
                        radius: card.radius
                        color: Theme.bgHover
                        opacity: cardArea.containsMouse ? 0.35 : 0
                        Behavior on opacity { NumberAnimation { duration: 120 } }
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.spL
                        spacing: Theme.spL

                        // ── 启用状态灯 ──
                        Rectangle {
                            width: 10; height: 10; radius: 5
                            Layout.alignment: Qt.AlignVCenter
                            color: card.isOn ? Theme.ok : Theme.textLo
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            spacing: 3

                            RowLayout {
                                spacing: 8
                                Label {
                                    text: card.modelData.name
                                    color: card.isOn ? Theme.textHi : Theme.textMid
                                    font.pixelSize: Theme.fsTitle
                                    font.bold: true
                                }
                                // 未启用角标
                                Rectangle {
                                    visible: !card.isOn
                                    Layout.alignment: Qt.AlignVCenter
                                    width: 52; height: 18
                                    radius: 9
                                    color: Theme.bgInput
                                    border.color: Theme.borderHi
                                    border.width: 1
                                    Text {
                                        anchors.centerIn: parent
                                        text: "未启用"
                                        color: Theme.textLo
                                        font.pixelSize: Theme.fsTiny
                                    }
                                }
                            }

                            Label {
                                text: card.modelData.summary
                                color: Theme.textMid
                                font.pixelSize: Theme.fsSmall
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            Label {
                                // 手动指定过就说清来源, 让用户知道当前值不是自动检测的
                                visible: card.modelData.userSet === true
                                text: "启用状态：手动指定"
                                color: Theme.textLo
                                font.pixelSize: Theme.fsTiny
                            }
                        }

                        Label {
                            Layout.alignment: Qt.AlignVCenter
                            text: "›"
                            color: Theme.textLo
                            font.pixelSize: Theme.fsBig
                        }
                    }

                    MouseArea {
                        id: cardArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: win.currentKey = card.modelData.key
                    }
                }
            }

            Item { Layout.preferredHeight: Theme.sp }
        }
    }
}
