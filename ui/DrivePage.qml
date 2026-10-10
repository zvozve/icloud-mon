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
            //
            // ★ 原来这里是 Layout.preferredHeight: 200 —— 写死的。
            //   但三个面板的实际内容只有"标题20 + 进度条8 + 详情34 ≈ 66px",
            //   剩下 130 多 px 全是空白。三个并排就是 **400px 的纯空白**,
            //   页面因此要滚很久才看到下面的内容。
            //   QueuePanel 内部是 ColumnLayout + anchors.fill, 本身就能
            //   按内容自撑高, 直接让 RowLayout 按子项 implicitHeight 排布。
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.sp

                QueuePanel {
                    Layout.fillWidth: true
                    // 不要 Layout.fillHeight: 面板高度由内容决定。
                    // RowLayout 会自动把同一行子项的高度对齐到最高者,
                    // 所以三个面板底边仍然齐平, 不用靠 fillHeight 硬撑。
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
                    // 不要 Layout.fillHeight: 面板高度由内容决定。
                    // RowLayout 会自动把同一行子项的高度对齐到最高者,
                    // 所以三个面板底边仍然齐平, 不用靠 fillHeight 硬撑。
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
                //
                // noProgress: 删除队列没有"进度"语义。原来 done/total 画出的
                // 进度条只要回收站非空就恒为满格, 加上 accent 是红色,
                // 于是一根红色满条长期挂在界面上, 看着像告警 —— 实际只是
                // "回收站里有几项、当前没有待删除"。改后标题右侧直接写
                // "回收站 N 项", 不再画进度条。
                QueuePanel {
                    Layout.fillWidth: true
                    // 不要 Layout.fillHeight: 面板高度由内容决定。
                    // RowLayout 会自动把同一行子项的高度对齐到最高者,
                    // 所以三个面板底边仍然齐平, 不用靠 fillHeight 硬撑。
                    title: "删除"
                    accent: Theme.err
                    done: Bridge.delDone
                    total: Bridge.delTotal
                    pendingCount: Bridge.delCount
                    pendingSize: ""
                    countLabel: "待删除"
                    noProgress: true
                    extraText: Bridge.delDone > 0
                                ? ("回收站 " + Bridge.delDone + " 项") : ""
                    busy: Bridge.delBusy
                    icon: "trash"
                }
            }

            // ── 云端状态分布 ──
            //
            // 不再写死 300(实际内容 ~170, 白占 130px)。
            // PcsPanel 自己按 buckets 条数算 implicitHeight。
            PcsPanel {
                Layout.fillWidth: true
            }

            // ── 待处理项明细 ──
            Rectangle {
                Layout.fillWidth: true
                // ★ 原来写死 360。实测绝大多数时候里面只有 0~2 条
                //   (正常同步状态就是 0 条), 却恒占 360px —— 页面因此要滚
                //   很久才看到下面的回收站, 而中间是一大片空白。
                //   改成按内容算高度:
                //     表头(约 20) + 本地未提交(最多 6 条 × 26) + 分隔线(1)
                //     + 云端列表(最多 8 行 × 26, 超过就滚动)
                //   再用上下限兜住: 太少不低于 110(表头+空态提示),
                //   太多不超过 340(否则又变成长空白)。中间值即所需高度。
                readonly property int localRows: Math.min(6, Bridge.localPendings.count)
                readonly property int cloudRows: Math.min(8, Bridge.pendingItems.count)
                Layout.preferredHeight: Math.max(110, Math.min(340,
                                     Theme.sp * 2
                                     + 20 + 6
                                     + localRows * 26
                                     + (localRows > 0 && cloudRows > 0 ? 7 : 0)
                                     + cloudRows * 26
                                     + 30))
                color: Theme.bgPanel
                radius: Theme.radius
                border.color: Theme.border
                border.width: 1
                // 必须 clip: 内部"本地未提交"是 Repeater(不可滚动),
                // 条目一多会超出面板。不裁剪的话内容会画到面板外面。
                clip: true

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
                            // 孤立记录(文件已删、状态未清)也单独报, 它不占队列。
                            text: Bridge.pendingRealCount + " 待处理"
                                  + (Bridge.pendingOrphanCount > 0
                                     ? " + " + Bridge.pendingOrphanCount + " 孤立"
                                     : "")
                                  + " + " + Bridge.localPendingTotal + " 本地未提交"
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
                            required property string path
                            id: lpRow
                            // 这个 delegate 属于 Repeater, 不是 ListView;
                            // 父级是 ColumnLayout, 宽度交给 Layout 管理。
                            Layout.fillWidth: true
                            height: 26
                            color: lpMouse.containsMouse ? Theme.bgHover
                                                         : Theme.bgCard
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
                                    text: lpRow.name
                                    color: Theme.textMid
                                    font.pixelSize: Theme.fsSmall
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }
                                Label {
                                    text: lpRow.stateText
                                    color: Theme.warn
                                    font.pixelSize: Theme.fsTiny
                                }
                            }

                            // 点整行在资源管理器里定位到该文件
                            MouseArea {
                                id: lpMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: Bridge.openItemLocation(lpRow.path)
                            }

                            // HoverHandler 的属性是 hovered, 没有 containsMouse
                            ToolTip.visible: lpMouse.containsMouse
                            ToolTip.text: lpRow.path !== ""
                                          ? (lpRow.name + "\n" + lpRow.path
                                             + "\n(点击定位)")
                                          : (lpRow.name + "\n(无路径)")
                            ToolTip.delay: 400
                        }
                    }

                    // 本地未提交只展示前几条, 超出部分用这行提示总数
                    Label {
                        Layout.fillWidth: true
                        visible: Bridge.localPendingTotal > Bridge.localPendings.count
                        text: "… 另有 " + (Bridge.localPendingTotal - Bridge.localPendings.count) + " 项本地未提交"
                        color: Theme.textLo
                        font.pixelSize: Theme.fsTiny
                        elide: Text.ElideRight
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
                            required property string path
                            required property bool missing
                            // delegate 声明了 required property 之后, index
                            // 不再自动注入, 必须自己声明
                            required property int index
                            id: piRow
                            width: ListView.view.width
                            height: 24
                            color: piMouse.containsMouse ? Theme.bgHover
                                : (index % 2 === 0 ? "transparent" : Theme.bgPanel)
                            // 孤立记录(文件已删)整体压暗: 它不是真在排队
                            opacity: piRow.missing ? 0.55 : 1.0

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 6
                                anchors.rightMargin: 6
                                spacing: 6

                                // 原来这里是 📁 / 📄 emoji: 彩色位图, 不跟主题色,
                                // 而且不同 Windows 版本的 emoji 字形差别很大。
                                // 换成与导航栏同一套的单色图元。
                                NavIcon {
                                    kind: piRow.isDir ? "folder" : "file"
                                    color: Theme.textLo
                                    Layout.alignment: Qt.AlignVCenter
                                }
                                Label {
                                    text: piRow.name
                                    color: piRow.missing ? Theme.textLo
                                                          : Theme.textMid
                                    font.pixelSize: Theme.fsSmall
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }
                                // 孤立记录明确标注, 免得以为同步卡住
                                Label {
                                    visible: piRow.missing
                                    text: "文件已删除"
                                    color: Theme.textLo
                                    font.pixelSize: Theme.fsTiny
                                }
                                Label {
                                    text: piRow.sizeText
                                    color: Theme.textLo
                                    font.pixelSize: Theme.fsTiny
                                }
                            }

                            // 点整行定位。该项可能还没落到本地, Bridge 会降级
                            // 打开最近的上级目录并提示。
                            MouseArea {
                                id: piMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: Bridge.openItemLocation(piRow.path)
                            }

                            ToolTip.visible: piMouse.containsMouse
                            ToolTip.text: piRow.missing
                                          ? (piRow.name
                                             + "\n文件已删除, iCloud 状态未清理干净"
                                             + "\n不占传输队列, 可忽略")
                                          : (piRow.path !== ""
                                             ? (piRow.name + "\n" + piRow.path
                                                + "\n(点击定位)")
                                             : (piRow.name + "\n(无路径, 无法定位)"))
                            ToolTip.delay: 400
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
                // 同待处理项: 原来写死 240, 空回收站时白占一整块。
                // 按内容算: 表头 + 最多 8 行 × 24(超过滚动) + 空态提示,
                // 下限 96(表头 + "回收站为空"), 上限 300。
                Layout.preferredHeight: Math.max(96, Math.min(300,
                                     Theme.sp * 2 + 20 + 6
                                     + Math.min(8, Bridge.trashItems.count) * 26
                                     + 30))
                color: Theme.bgPanel
                radius: Theme.radius
                border.color: Theme.border
                border.width: 1
                clip: true

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
                            id: trRow
                            width: ListView.view.width
                            height: 28
                            color: trMouse.containsMouse ? Theme.bgHover
                                : (index % 2 === 0 ? "transparent" : Theme.bgCard)

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 6
                                anchors.rightMargin: 6
                                spacing: 6

                                Label {
                                    text: trRow.name
                                    color: Theme.textMid
                                    font.pixelSize: Theme.fsSmall
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }
                                Label {
                                    // 原始路径: 光看名字认不出是什么,
                                    // 原路径才能判断该不该恢复
                                    text: "原位于 " + trRow.restorePath
                                    color: Theme.textLo
                                    font.pixelSize: Theme.fsTiny
                                    elide: Text.ElideLeft
                                    Layout.preferredWidth: 240
                                }
                            }

                            // 点整行打开该文件删除前所在的目录。
    // 用 openTrashLocation 而不是 openItemLocation: 回收站项的文件已经
    // 被删除, 原路径下没有它, 定位不到文件本身, 打开原目录才有意义。
                            MouseArea {
                                id: trMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: Bridge.openTrashLocation(trRow.restorePath)
                            }

                            ToolTip.visible: trMouse.containsMouse
                            ToolTip.text: "原名: " + trRow.name
                                          + "\n原路径: " + trRow.restorePath
                                          + "\n(已删除, 点击打开原所在目录)"
                            ToolTip.delay: 400
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
