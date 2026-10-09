// ======================================================================
//  Main.qml  -  主界面
//
//  布局:
//    顶栏  : 标题 + iCloud 运行开关 + 设置按钮
//    横幅  : 异常告警 (仅异常时出现)
//    状态卡: 当前状态大标题 + 同步游标
//    上/下载: 两个队列面板 (完成数/总数)
//    云端分布: pcs_state 分布条
//    待处理项: 明细列表
// ======================================================================
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

ApplicationWindow {
    id: win
    width: 1080
    height: 820
    minimumWidth: 900
    minimumHeight: 640
    visible: true
    title: Bridge.stateName === "未知" ? "iCloud 状态监控"
                                       : "iCloud 状态监控 - " + Bridge.stateName
    color: Theme.bgRoot

    // ── 状态栏提示 ──
    property string toastText: ""
    property string toastKind: "info"
    property int toastTimer: 0

    function showToast(kind, text) {
        toastKind = kind
        toastText = text
        toastTimer.restart()
    }

    Connections {
        target: Bridge
        function onNotify(kind, text) {
            win.showToast(kind, text)
        }
        function onIcloudStateChanged() {
            // 重启后进程状态变了, 立刻刷一次界面
            win.ipcTimer.restart()
        }
    }

    Timer {
        id: toastTimer
        interval: 4000
        onTriggered: win.toastText = ""
    }

    // 进程状态变化后延迟一点再采同步数据, 等 iCloud 起来
    Timer {
        id: ipcTimer
        interval: 4000
        onTriggered: Bridge.refreshNow()
    }

    // ══════════════════════════════════════════════════════════════
    //  顶栏
    // ══════════════════════════════════════════════════════════════
    header: Rectangle {
        height: 60
        color: Theme.bgPanel

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.border
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spL
            anchors.rightMargin: Theme.spL
            spacing: Theme.sp

            // 状态指示灯
            Rectangle {
                width: 10; height: 10; radius: 5
                color: Theme.stateColor(Bridge.stateName)
                Behavior on color { ColorAnimation { duration: 300 } }
            }

            Label {
                text: "iCloud 同步监控"
                color: Theme.textHi
                font.pixelSize: Theme.fsTitle
                font.bold: true
            }

            Label {
                text: Bridge.dbOk ? ("v" + Bridge.clientVersion) : "数据库不可读"
                color: Theme.textLo
                font.pixelSize: Theme.fsSmall
            }

            Item { Layout.fillWidth: true }

            // ── iCloud 开关 (核心操作) ──
            RowLayout {
                spacing: 6

                Label {
                    text: Bridge.icloudRunning ? "iCloud 运行中" : "iCloud 已停止"
                    color: Bridge.icloudRunning ? Theme.ok : Theme.err
                    font.pixelSize: Theme.fsSmall
                    font.bold: true
                }

                Switch {
                    id: icloudSwitch
                    // 开关的 checked 状态完全由后端进程事实驱动,
                    // 用户点击 -> 调 restart 而不是直接改 checked,
                    // 避免出现"界面显示开着但实际没运行"的假状态。
                    checked: Bridge.icloudRunning
                    enabled: !Bridge.busyRestarting
                    onToggled: {
                        if (checked !== Bridge.icloudRunning) {
                            Bridge.restartIcloud()
                            // 立刻弹回, 等后端确认后再由 onIcloudStateChanged 更新
                            checked = Bridge.icloudRunning
                        }
                    }

                    contentItem: Rectangle {
                        implicitWidth: 42
                        implicitHeight: 20
                        radius: 10
                        color: icloudSwitch.checked ? Theme.ok : Theme.bgInput
                        border.color: icloudSwitch.checked ? Theme.ok : Theme.borderHi
                        opacity: icloudSwitch.enabled ? 1.0 : 0.5

                        Behavior on color { ColorAnimation { duration: 160 } }

                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            x: icloudSwitch.checked
                               ? parent.width - width - 2 : 2
                            width: 16; height: 16; radius: 8
                            color: "#ffffff"
                            Behavior on x {
                                NumberAnimation { duration: 160; easing.type: Easing.OutCubic }
                            }
                        }
                    }
                }

                Button {
                    id: restartBtn
                    text: Bridge.busyRestarting ? "重启中..." : "重启"
                    enabled: !Bridge.busyRestarting
                    onClicked: Bridge.restartIcloud()
                    ToolTip.visible: hovered
                    ToolTip.text: "结束全部 iCloud 进程后重新启动"
                    ToolTip.delay: 600

                    contentItem: Label {
                        text: restartBtn.text
                        color: restartBtn.enabled ? Theme.textHi : Theme.textLo
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.pixelSize: Theme.fsSmall
                    }
                    background: Rectangle {
                        implicitWidth: 72
                        implicitHeight: 30
                        radius: Theme.radius
                        color: restartBtn.hovered ? Theme.bgHover : Theme.bgCard
                        border.color: Theme.borderHi
                    }
                }
            }

            // ── 打开 iCloud 目录 ──
            Button {
                id: openDriveBtn
                width: 34; height: 30
                onClicked: Bridge.openDriveRoot()
                ToolTip.visible: hovered
                ToolTip.text: "打开 iCloud 目录\n" + Bridge.driveRoot
                ToolTip.delay: 600

                contentItem: Text {
                    text: "📂"                       // 文件夹
                    color: openDriveBtn.hovered ? Theme.infoHi : Theme.textMid
                    font.pixelSize: 15
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: Theme.radius
                    color: openDriveBtn.hovered ? Theme.bgHover : "transparent"
                    border.color: openDriveBtn.hovered ? Theme.borderHi : "transparent"
                }
            }

            // ── 设置按钮 ──
            Button {
                id: settingsBtn
                width: 34; height: 30
                onClicked: settingsPage.open()
                ToolTip.visible: hovered
                ToolTip.text: "设置"
                ToolTip.delay: 600

                contentItem: Text {
                    text: "⚙"                       // 齿轮
                    color: settingsBtn.hovered ? Theme.textHi : Theme.textMid
                    font.pixelSize: 17
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: Theme.radius
                    color: settingsBtn.hovered ? Theme.bgHover : "transparent"
                    border.color: settingsBtn.hovered ? Theme.borderHi : "transparent"
                }
            }
        }
    }

    // ══════════════════════════════════════════════════════════════
    //  主体 (包在ScrollView 里)
    //
    //  为什么必须加滚动容器: 原来主体是 ColumnLayout + anchors.fill,
    //  窗口一缩小底部内容就被裁掉且无法访问。
    //
    //  为什么用 Flickable 而不是 ScrollView 直接包ColumnLayout:
    //  ScrollView 会把 ColumnLayout 当 childItem, 按 child 的 implicitHeight
    //  算高度; 但 ColumnLayout 又被 anchors.fill 约束, 两者打架会导致
    //  高度算不出来。显式写成 contentWidth/contentHeight 最直接可控。
    // ══════════════════════════════════════════════════════════════
    Flickable {
        id: body
        anchors.fill: parent
        anchors.margins: Theme.spL
        contentWidth: width
        // 内容高度: 不足一屏时取可见高度(不出现多余滚动条), 超出则可滚动。
        //
        // ★ 关键: 必须把这个高度**真正赋给下面的 ColumnLayout**。
        // 只给宽不给高的列布局没有"剩余空间"可分, 里面的
        // Layout.fillHeight 全部失效 —— 而 PcsPanel / 待处理项 都是
        // Rectangle + anchors.fill 的子项, Rectangle 不会从子项推导
        // implicitHeight, 结果就是底部整行塌成 0 高、内容溢出重叠。
        readonly property real innerHeight:
            Math.max(height, bodyLayout.implicitHeight + Theme.sp)
        contentHeight: innerHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        // 只在需要时才显示滚动条, 且不一直占位导致内容宽度跳动
        ScrollBar.vertical: ScrollBar {
            policy: body.contentHeight > body.height + 1 ? ScrollBar.AsNeeded
                                                        : ScrollBar.AlwaysOff
        }

        ColumnLayout {
            id: bodyLayout
            width: body.width
            height: body.innerHeight   // 给了确定高度, fillHeight 才有意义
            spacing: Theme.sp

        // ── 异常横幅 ──
        AlertBanner {
            id: banner
            Layout.fillWidth: true
            visible: Bridge.anomalous
            // 折叠动画: 出现/隐藏都有过渡, 不做突兀的跳变
            Layout.preferredHeight: visible ? implicitHeight : 0
            Behavior on Layout.preferredHeight {
                NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
            }
        }

        // ── 状态卡 ──
        StatusCard {
            Layout.fillWidth: true
        }

        // ── 上/下载 队列 ──
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 172
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
                icon: "↑"
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
                icon: "↓"
            }
        }

        // ── 云端分布 + 待处理项 ──
        RowLayout {
            Layout.fillWidth: true
            // 必须给一个具体高度: 子项是 Rectangle + anchors.fill,
            // Rectangle 没有 implicitHeight, 整行算不出内容高度,
            // 后果是 ColumnLayout.implicitHeight 偏小 -> 滚动高度算不出来,
            // 且 fillHeight 没东西可分 -> 整行塌陷重叠。
            // fillHeight 保留: 窗口拉高时这一行还能跟着长。
            Layout.preferredHeight: 300
            Layout.fillHeight: true
            spacing: Theme.sp

            PcsPanel {
                Layout.fillWidth: true
                Layout.fillHeight: true
            }

            // 待处理项明细
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
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
                            // 混在一起会让人以为有十几个任务卡着。
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

                    // 本地未提交项 (卡死源头, 优先展示)
                    Repeater {
                        model: Bridge.localPendings
                        delegate: Rectangle {
                            required property string name
                            required property string stateText
                            // 这个 delegate 属于 Repeater, 不是 ListView ——
                            // 写 ListView.view.width 会取到 null, 报
                            // "Cannot read property 'width' of null"。
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
                            // (那是 MouseArea 的)。写错会得到 undefined, 报
                            // "Unable to assign [undefined] to bool"。
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

                    // 云端待处理项
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
                            // delegate 一旦声明了 required property, QML 就不再
                            // 自动注入 index 上下文属性, 必须自己声明一项。
                            // 否则 index 未定义, 报 "ReferenceError: index is not defined"。
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

                    // 空状态
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
        // 关闭 bodyLayout(ColumnLayout)
    }
    // 关闭 body(Flickable)
    }

    // ══════════════════════════════════════════════════════════════
    //  设置页
    // ══════════════════════════════════════════════════════════════
    SettingsPage {
        id: settingsPage
        anchors.fill: parent
        visible: false
        z: 100
    }

    // ══════════════════════════════════════════════════════════════
    //  Toast (右下角临时提示)
    // ══════════════════════════════════════════════════════════════
    Rectangle {
        id: toast
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spL

        // 宽度用**固定值**, 不要去测量内部 RowLayout 的 implicitWidth。
        // 这里踩过一个绑定循环: toastRow 是 anchors.fill: parent,
        // 它的宽度来自 toast.width; 而 RowLayout 内含 wrapMode 的 Label,
        // 其 implicitWidth 又依赖被分配的宽度 -> toast.width 与
        // toastRow.implicitWidth 互相引用, 运行时直接报
        // "QML Rectangle: Binding loop detected for property width"。
        // 固定宽度后, 文本在该宽度内换行, 高度由内容决定, 无循环。
        readonly property int toastW: 360
        width: toastText.length > 0 ? toastW : 0
        // 高度 = 内容高 + 上下各 Theme.sp(与 toastRow 的 anchors.margins 对应),
        // 少给一份会把文字挤掉 8px
        height: toastText.length > 0 ? toastRow.implicitHeight + Theme.sp * 2 : 0
        visible: opacity > 0
        opacity: toastText.length > 0 ? 1 : 0
        color: Theme.bgCard
        radius: Theme.radius
        border.color: toastKind === "error" ? Theme.err
                     : toastKind === "warn"  ? Theme.warn
                     : toastKind === "ok"    ? Theme.ok : Theme.infoHi
        border.width: 1
        z: 200

        Behavior on opacity { NumberAnimation { duration: 200 } }
        // Behavior 没有 when 属性(只有 enabled), 之前写成 when 是错的。
        // 平时 toast 不可见, 关掉 width 动画的参与。
        Behavior on width {
            enabled: win.toastText.length > 0
            NumberAnimation { duration: 180 }
        }

        RowLayout {
            id: toastRow
            anchors.fill: parent
            anchors.margins: Theme.sp
            spacing: 8

            Rectangle {
                width: 7; height: 7; radius: 3.5
                color: toastKind === "error" ? Theme.err
                     : toastKind === "warn"  ? Theme.warn
                     : toastKind === "ok"    ? Theme.ok : Theme.info
            }
            Label {
                text: win.toastText
                color: Theme.textHi
                font.pixelSize: Theme.fsSmall
                wrapMode: Text.WordWrap
                Layout.maximumWidth: 380
            }
        }

        // 点击关闭
        MouseArea {
            anchors.fill: parent
            onClicked: win.toastText = ""
        }
    }
}
