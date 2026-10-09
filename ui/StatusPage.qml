// ======================================================================
//  StatusPage.qml  -  状态页（详情）
//
//  回答一个问题: 具体是哪些文件在等?
//    - 云端状态分布 (server_items.pcs_state)
//    - 待处理项明细: 本地未提交项(卡死源头) + 云端待处理项
//  「现在什么状态」放首页。
// ======================================================================
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: page

    // 用 Flickable 而不是直接 anchors.fill + ColumnLayout:
    // 窗口变矮时内容能被滚动到, 而不是被裁掉。
    // 注意: 里面的 ColumnLayout 必须拿到**确定高度**(height: body.innerHeight),
    // 否则它没有"剩余空间"可分, 内部的 Layout.fillHeight 会全部失效 ——
    // 子项是 Rectangle 时没有 implicitHeight, 会直接塌成 0 高并重叠。
    Flickable {
        id: body
        anchors.fill: parent
        anchors.margins: Theme.spL
        contentWidth: width
        readonly property real innerHeight:
            Math.max(height, col.implicitHeight + Theme.sp)
        contentHeight: innerHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ScrollBar.vertical: ScrollBar {
            policy: body.contentHeight > body.height + 1 ? ScrollBar.AsNeeded
                                                        : ScrollBar.AlwaysOff
        }

        ColumnLayout {
            id: col
            width: body.width
            height: body.innerHeight
            spacing: Theme.sp

            // ── 云端状态分布 ──
            PcsPanel {
                Layout.fillWidth: true
                Layout.preferredHeight: 300
            }

            // ── 待处理项明细 ──
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: 360
                color: Theme.bgPanel
                radius: Theme.radius
                border.color: Theme.border
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.sp
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: "待处理项"
                            color: Theme.textHi
                            font.pixelSize: Theme.fsBody
                            font.bold: true
                        }
                        Item { Layout.fillWidth: true }
                        Label {
                            // "仅云端"必须和"待处理"分开说清: 那只是本地没有
                            // 实体(iCloud 按需下载), 属正常状态, 不是待办。
                            text: Bridge.pendingRealCount + " 待处理 + "
                                  + Bridge.localPendings.count + " 本地未提交"
                            color: Bridge.pendingRealCount > 0 ? Theme.warn
                                                               : Theme.textLo
                            font.pixelSize: Theme.fsTiny
                        }
                        Label {
                            visible: Bridge.cloudOnlyCount > 0
                            text: "· 仅云端 " + Bridge.cloudOnlyCount
                                  + " 项(按需下载)"
                            color: Theme.textLo
                            font.pixelSize: Theme.fsTiny
                        }
                    }

                    // ── 本地未提交项(卡死源头, 优先展示) ──
                    Repeater {
                        model: Bridge.localPendings

                        delegate: Rectangle {
                            required property string name
                            required property string stateText
                            // 这个 delegate 属于 Repeater, 不是 ListView ——
                            // 写 ListView.view.width 会取到 null。
                            // 父级是 ColumnLayout, 宽度交给 Layout 管理。
                            Layout.fillWidth: true
                            height: 26
                            color: Theme.bgCard
                            radius: 3

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 6
                                anchors.rightMargin: 6
                                spacing: 6

                                Rectangle {
                                    width: 5; height: 5; radius: 2.5
                                    color: Theme.warn
                                }
                                Label {
                                    text: name
                                    color: Theme.textMid
                                    font.pixelSize: Theme.fsSmall
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }
                                Label {
                                    text: stateText
                                    color: Theme.warn
                                    font.pixelSize: Theme.fsTiny
                                }
                            }

                            // HoverHandler 的属性是 hovered, 没有 containsMouse
                            ToolTip.visible: nameArea.hovered
                            ToolTip.text: name
                            HoverHandler { id: nameArea }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: Theme.border
                        visible: Bridge.localPendings.count > 0
                                && Bridge.pendingItems.count > 0
                    }

                    // ── 云端待处理项 ──
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: Bridge.pendingItems
                        spacing: 2
                        ScrollBar.vertical: ScrollBar {}

                        delegate: Rectangle {
                            required property string name
                            required property string sizeText
                            required property bool isDir
                            // delegate 声明了 required property 之后, index
                            // 不再自动注入, 必须自己声明
                            required property int index
                            width: ListView.view.width
                            height: 24
                            color: index % 2 === 0 ? "transparent" : Theme.bgPanel

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 6
                                anchors.rightMargin: 6
                                spacing: 6

                                Label {
                                    text: isDir ? "📁" : "📄"
                                    font.pixelSize: Theme.fsSmall
                                }
                                Label {
                                    text: name
                                    color: Theme.textMid
                                    font.pixelSize: Theme.fsSmall
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }
                                Label {
                                    text: sizeText
                                    color: Theme.textLo
                                    font.pixelSize: Theme.fsTiny
                                }
                            }
                        }
                    }

                    // ── 空状态 ──
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.topMargin: 20
                        text: Bridge.pendingRealCount === 0
                              && Bridge.localPendings.count === 0
                              ? "无待处理项，同步已追平" : ""
                        visible: text !== ""
                        color: Theme.textLo
                        font.pixelSize: Theme.fsSmall
                    }
                }
            }
        }
    }
}
