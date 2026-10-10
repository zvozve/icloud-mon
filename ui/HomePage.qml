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

    // 只显示**已启用**的类别。需求: 设置里关掉的类别, 主页与侧边栏一致隐藏。
    // 直接给 Repeater 一个过滤后的数组即可 —— 委托仍然拿 modelData.enabled
    // 等字段, 结构不变。
    readonly property var enabledCats: {
        var list = Bridge.categories
        var out = []
        for (var i = 0; i < list.length; ++i)
            if (list[i].enabled === true) out.push(list[i])
        return out
    }

    Flickable {
        id: body
        anchors.fill: parent
        contentWidth: width
        // 不要把自身 height 掺进来。Math.max(height, ...) 会给布局留下回环,
        // 实测软件渲染后端下 Qt 会报
        //   QML Flickable: Binding loop detected for property "contentHeight".
        // 而且它本来就多余: 内容不足一屏时下面的 ScrollBar policy 自己会判
        // 成 AlwaysOff, 不需要靠撑大 contentHeight 来消除滚动条。
        // 上下留白必须与 col 的 y 偏移(Theme.spXL)和底部收尾一致,
        // 否则底部会多出或少掉一段空白。
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

            // ── iCloud 控制区 ──
            //
            // ★ 原先这些控件在顶部固定栏(顶栏)里, 但顶栏是所有页面共用的,
            //   "启停 iCloud"这种全局操作挂在每个页面的顶栏上并不合适
            //   (书签页/密码页顶部也摆一个 iCloud 开关, 与该页内容无关)。
            //   现移到首页: 首页是应用的入口页, 全局操作放这里最自然。
            // ── iCloud 运行状态面板(开关 + 进程 + 游标, 合并为一个) ──
            //
            // ★ 原来这里是**两个**独立面板: 上面的"开关组"和下面的
            //   "进程+游标"。虽然是同一条信息链(开关 → 实际跑了哪些进程
            //   → 游标是否追平), 但拆成两块就被上下边框割裂, 看起来像
            //   两件不相关的事。已合并成一个面板, 内部两行 —— 上边框只画
            //   一次, 进程与开关的因果关系连着读下来。
            //
            //   高度按内容算(内部 ColumnLayout 自撑): 第一行约 30,
            //   第二行(进程+游标同排)约 18, 加边距与行距。
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: statusCol.implicitHeight + Theme.spL * 2
                color: Theme.bgPanel
                radius: Theme.radius
                border.color: Theme.border
                border.width: 1

                ColumnLayout {
                    id: statusCol
                    anchors.fill: parent
                    anchors.margins: Theme.spL
                    spacing: Theme.sp

                    // ── 第一行: iCloud + 启停开关 + 版本 + 采样 + 刷新 ──
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: Theme.sp

                        Label {
                            text: "iCloud"
                            color: Theme.textHi
                            font.pixelSize: Theme.fsBig
                            font.bold: true
                        }

                        Rectangle {
                            width: 1; height: 20
                            color: Theme.border
                            Layout.alignment: Qt.AlignVCenter
                        }

                        Label {
                            text: Bridge.icloudRunning ? "运行中" : "已停止"
                            color: Bridge.icloudRunning ? Theme.ok : Theme.err
                            font.pixelSize: Theme.fsBody
                            font.bold: true
                        }

                        AppSwitch {
                            id: icloudSwitch
                            // ★ 开关状态完全由后端进程事实驱动。
                            //   **绝对不要在这里写 `checked = ...`** ——
                            //   那是对绑定属性的 JS 赋值, 会把
                            //   `checked: Bridge.icloudRunning` 这个绑定删除
                            //   (QML 规则: C++ 内部写属性不移除绑定, JS 赋值会)。
                            //   绑定一旦被删, checked 永久停在用户最后拨到的值,
                            //   不再跟随真实进程状态。点击只调后端动作,
                            //   由 onProcTimer 经 icloudStateChanged 推回来。
                            checked: Bridge.icloudRunning
                            enabled: !Bridge.busyRestarting
                            onToggled: {
                                if (checked) Bridge.startIcloud()
                                else         Bridge.stopIcloud()
                            }
                            ToolTip.visible: hovered
                            ToolTip.text: Bridge.icloudRunning
                                          ? "点一下停止 iCloud"
                                          : "点一下启动 iCloud"
                            ToolTip.delay: 500
                        }

                        Item { Layout.fillWidth: true }

                        Label {
                            text: Bridge.dbOk ? ("v" + Bridge.clientVersion)
                                              : "数据库不可读"
                            color: Bridge.dbOk ? Theme.textLo : Theme.err
                            font.pixelSize: Theme.fsSmall
                        }

                        Label {
                            // 采样时刻: **整个程序**的采样点(所有页面的数据
                            // 都来自这一次), 属软件状态, 所以放首页。
                            text: "更新 " + Bridge.timestamp
                            color: Theme.textLo
                            font.pixelSize: Theme.fsSmall
                        }

                        AppButton {
                            kind: "ghost"
                            minW: 60
                            padH: 6
                            text: "刷新"
                            onClicked: Bridge.refreshNow()
                            ToolTip.visible: hovered
                            ToolTip.text: "立即刷新 (每 " + Bridge.refreshSec + " 秒自动)"
                            ToolTip.delay: 500
                        }
                    }

                    // ── 第二行: 进程 + 游标(同一排, 用弹性空隙隔开) ──
                    //
                    // 进程与游标都是"开关/程序运行得好不好"的佐证, 并列一行
                    // 比上下两行更紧凑, 也让上面那排控件的宽度压力小一些。
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 6
                        // 进程与游标任一为空时整行隐藏, 避免出现空行
                        visible: Bridge.icloudProcesses !== "" || Bridge.rankText !== ""

                        Label {
                            text: "进程"
                            color: Theme.textLo
                            font.pixelSize: Theme.fsTiny
                            visible: Bridge.icloudProcesses !== ""
                        }
                        Label {
                            text: Bridge.icloudProcesses
                            color: Bridge.icloudRunning ? Theme.textMid : Theme.err
                            font.pixelSize: Theme.fsTiny
                            font.family: "Consolas, monospace"
                            // ★ 宽度分配: 进程用 Layout.fillWidth(可压缩), 但把**最小宽度压到 0**;
                            //   游标给一个明确的 minimumWidth, 保证它不会被压没。
                            //   原来两者都 fillWidth + minimumWidth:0 等分, 结果
                            //   进程被截成 "ApplePhotoSt..."、游标被截成 "-> ..."
                            //   (结尾"已追平"看不到), 两边都没读全。
                            //   现在: 进程可让、游标优先完整 —— 少看一个 exe 名
                            //   不影响判断, 但"已追平/未追平"是结论, 必须看到。
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            Layout.preferredWidth: 3
                            // 进程名可能很长(4 个 exe 逗号连接),
                            // 必须能省略收尾, 否则会把布局撑开
                            elide: Text.ElideRight
                            visible: Bridge.icloudProcesses !== ""
                        }

                        Rectangle {
                            width: 1; height: 12
                            color: Theme.border
                            Layout.alignment: Qt.AlignVCenter
                            visible: Bridge.icloudProcesses !== ""
                                           && Bridge.rankText !== ""
                        }

                        Label {
                            text: Bridge.inSync ? "\u2713" : "\u27F3"
                            color: Bridge.inSync ? Theme.ok : Theme.warn
                            font.pixelSize: Theme.fsSmall
                            font.bold: true
                            visible: Bridge.rankText !== ""
                        }
                        Label {
                            text: Bridge.rankText
                            color: Theme.textLo
                            font.pixelSize: Theme.fsTiny
                            font.family: "Consolas, monospace"
                            // 游标设一个最小宽度(150px 够放下
                            // "client=2522 server=2522 待应用=0 -> 已追平"),
                            // 这样空间不够时被压缩的是进程名(可省略),
                            // 游标的"已追平/未追平"结论永远可见。
                            Layout.minimumWidth: 150
                            elide: Text.ElideRight
                            visible: Bridge.rankText !== ""
                        }
                    }
                }
            }

            Label {
                text: "点卡片进入对应页面看细节。未启用的类别已隐藏(可在设置里改)。"
                color: Theme.textLo
                font.pixelSize: Theme.fsSmall
            }

            // ── 卡死告警(软件自身状态) ──
            //
            // ★ 从云盘页搬到这里。
            //   卡死/进程/游标是**软件与 iCloud 进程的状态**, 虽然数据来自
            //   iCloudDrive.db, 但它描述的是"程序运行得怎么样", 而不是
            //   "云盘里有什么文件"。放在云盘页会让人误以为只有打开云盘页
            //   才能看到告警 —— 出问题时最该第一时间看到的地方反而是首页。
            AlertBanner {
                Layout.fillWidth: true
                visible: Bridge.anomalous
                Layout.preferredHeight: visible ? implicitHeight : 0
                Behavior on Layout.preferredHeight {
                    NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
                }
            }

            Repeater {
                model: page.enabledCats

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
