// ======================================================================
//  DrivePage.qml  -  云盘页
//
//  云盘的全部细节都在这:
//    异常横幅 + 状态卡 + 上传/下载/删除 三队列
//    + 云端分布 + 待处理项 + 回收站
//  (原来拆在"概览"和"状态"两页, 现在都归属云盘)
//
//  所有子项都给**固定高度**, 不用 Layout.fillHeight —— 在 Flickable 里
//  填高没有意义(没有剩余空间可分), 而且子项是 Rectangle 时没有
//  implicitHeight, 会直接塌成 0 高并重叠。
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
        // 不要把自身 height 掺进来。Math.max(height, ...) 会给布局留下回环,
        // 实测软件渲染后端下 Qt 会报
        //   QML Flickable: Binding loop detected for property "contentHeight".
        // 而且它本来就多余: 内容不足一屏时下面的 ScrollBar policy 自己会判
        // 成 AlwaysOff, 不需要靠撑大 contentHeight 来消除滚动条。
        contentHeight: col.implicitHeight + Theme.spL * 2
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

            // ── 异常横幅 ──
            AlertBanner {
                Layout.fillWidth: true
                visible: Bridge.anomalous
                Layout.preferredHeight: visible ? implicitHeight : 0
                Behavior on Layout.preferredHeight {
                    NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
                }
            }

            // ── 状态卡 ──
            StatusCard {
                Layout.fillWidth: true
            }

            // ── 上传 / 下载 / 删除 三个队列 ──
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 200
                spacing: Theme.sp

                QueuePanel {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    title: "上传"
                    accent: Theme.up
                    done: Bridge.upDone
                    total: Bridge.upTotal
                    pendingCount: Bridge.upCount
                    pendingSize: Bridge.upSize
                    busy: Bridge.upBusy
                    icon: "upload"
                }

                QueuePanel {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    title: "下载"
                    accent: Theme.down
                    done: Bridge.dlDone
                    total: Bridge.dlTotal
                    pendingCount: Bridge.dlCount
                    pendingSize: Bridge.dlSize
                    extraText: Bridge.dlPending > 0
                                ? ("取回队列 " + Bridge.dlPending + " 条") : ""
                    busy: Bridge.dlBusy
                    icon: "download"
                }

                // 删除: 待删除 = needs_delete_doc_count;
                // "已完成"侧用回收站项数对应(表里没有 deleted_count)。
                // 删除没有字节数概念, pendingSize 传空串 -> 那一栏自动隐藏。
                QueuePanel {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    title: "删除"
                    accent: Theme.err
                    done: Bridge.delDone
                    total: Bridge.delTotal
                    pendingCount: Bridge.delCount
                    pendingSize: ""
                    countLabel: "待删除"
                    extraText: Bridge.delDone > 0
                                ? ("回收站 " + Bridge.delDone + " 项") : ""
                    busy: Bridge.delBusy
                    icon: "trash"
                }
            }

            // ── 云端状态分布 ──
            PcsPanel {
                Layout.fillWidth: true
                Layout.preferredHeight: 300
            }

            // ── 待处理项明细 ──
            Rectangle {
                Layout.fillWidth: true
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
                            // 这个 delegate 属于 Repeater, 不是 ListView;
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

                                // 原来这里是 📁 / 📄 emoji: 彩色位图, 不跟主题色,
                                // 而且不同 Windows 版本的 emoji 字形差别很大。
                                // 换成与导航栏同一套的单色图元。
                                NavIcon {
                                    kind: isDir ? "folder" : "file"
                                    color: Theme.textLo
                                    Layout.alignment: Qt.AlignVCenter
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

            // ── 回收站(删除队列的另一面) ──
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 240
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
                            text: "回收站"
                            color: Theme.textHi
                            font.pixelSize: Theme.fsBody
                            font.bold: true
                        }
                        Item { Layout.fillWidth: true }
                        Label {
                            text: Bridge.trashItems.count + " 项（可恢复）"
                            color: Theme.textLo
                            font.pixelSize: Theme.fsTiny
                        }
                    }

                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: Bridge.trashItems
                        spacing: 2
                        ScrollBar.vertical: ScrollBar {}

                        delegate: Rectangle {
                            required property string name
                            required property string restorePath
                            required property int index
                            width: ListView.view.width
                            height: 28
                            color: index % 2 === 0 ? "transparent" : Theme.bgCard

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 6
                                anchors.rightMargin: 6
                                spacing: 6

                                Label {
                                    text: name
                                    color: Theme.textMid
                                    font.pixelSize: Theme.fsSmall
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }
                                Label {
                                    // 原始路径: 光看名字认不出是什么,
                                    // 原路径才能判断该不该恢复
                                    text: "原位于 " + restorePath
                                    color: Theme.textLo
                                    font.pixelSize: Theme.fsTiny
                                    elide: Text.ElideLeft
                                    Layout.preferredWidth: 240
                                }
                            }

                            ToolTip.visible: trashHover.hovered
                            ToolTip.text: "原名: " + name + "\n原路径: " + restorePath
                            ToolTip.delay: 500
                            HoverHandler { id: trashHover }
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.topMargin: 12
                        text: Bridge.trashItems.count === 0 ? "回收站为空" : ""
                        visible: text !== ""
                        color: Theme.textLo
                        font.pixelSize: Theme.fsSmall
                    }
                }
            }

            Item { Layout.preferredHeight: Theme.sp }
        }
    }
}
