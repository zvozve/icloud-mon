// ======================================================================
//  Main.qml  -  主界面（仿 UWP NavigationView，6 个页面）
//
//  ┌──────────┬──────────────────────────────────────┐
//  │ ☰ 标题   │  命令栏: 状态点/版本   iCloud 开关 …  │
//  │ ──────── ├──────────────────────────────────────┤
//  │ ⌗ 首页   │                                      │
//  │ ☁ 云盘   │      页面内容（StackLayout 切页）      │
//  │ ▣ 相册   │                                      │
//  │ ⚿ 密码   │                                      │
//  │ ⊞ 书签   │                                      │
//  │ ⚙ 设置   │                                      │
//  └──────────┴──────────────────────────────────────┘
//
//  · 首页固定最前、设置固定最后, 中间四类顺序由 Bridge.navOrder 决定
//    (可在设置页调整, 存 QSettings)
//  · 用 StackLayout 而不是 Loader: StackLayout 的子项顺序固定, 侧边栏换序
//    只影响导航显示, 不用重算索引映射, 简单且不会出错
//  · 默认启动页来自 Bridge.defaultPage, 只在启动时取一次
//    (不做成绑定, 否则用户一改设置当前页就被拽走)
// ======================================================================
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

ApplicationWindow {
    id: win
    width: 1100
    height: 840
    minimumWidth: 860
    minimumHeight: 620
    visible: true
    // 标题栏带版本号: 排障时一眼确认跑的是哪个 build。
    // AppVersion 由 main.cpp 注入(= CMake project VERSION)。
    title: (Bridge.stateName === "未知"
            ? "iCloud 状态监控 v" + AppVersion
            : "iCloud 状态监控 v" + AppVersion + " - " + Bridge.stateName)
    color: Theme.bgRoot

    // 导航栏是否展开(仅图标 <-> 图标+文字)
    property bool navExpanded: true
    // 当前页面 key: home | drive | photos | passwords | bookmarks | settings
    property string currentKey: "home"

    // 启动时按用户设置的默认启动页定位
    Component.onCompleted: currentKey = Bridge.defaultPage

    // 侧边栏实际显示的类别顺序 = navOrder 里**已启用**的那些。
    // 需求: 在设置里把某类关掉后, 侧边栏与主页都要真正隐藏它(不是灰化)。
    // 这里同时读 categories 和 navOrder, 两者任一变化都会重算;
    // 类别状态尚未采样到(列表为空)时一律按"启用"处理, 避免启动瞬间空侧边栏。
    readonly property var visibleOrder: {
        var cats = Bridge.categories
        var order = Bridge.navOrder
        var out = []
        for (var i = 0; i < order.length; ++i) {
            var enabled = true
            for (var j = 0; j < cats.length; ++j) {
                if (cats[j].key === order[i]) { enabled = cats[j].enabled === true; break }
            }
            if (enabled) out.push(order[i])
        }
        return out
    }

    // 当前页被隐藏(用户刚把它关掉了)时退回首页, 否则会停在一个
    // 侧边栏里已经没有入口的页面上。
    onVisibleOrderChanged: {
        if (currentKey !== "home" && currentKey !== "settings"
            && visibleOrder.indexOf(currentKey) < 0)
            currentKey = "home"
    }

    // 类别 key -> 图标种类
    function iconFor(k) {
        switch (k) {
        case "drive":     return "drive"
        case "photos":    return "photos"
        case "passwords": return "passwords"
        case "bookmarks": return "bookmarks"
        }
        return "status"
    }

    // ── Toast 状态 ──
    property string toastText: ""
    property string toastKind: "info"

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
            win.ipcTimer.restart()
        }
    }

    Timer {
        id: toastTimer
        interval: 4000
        onTriggered: win.toastText = ""
    }

    Timer {
        id: ipcTimer
        interval: 4000
        onTriggered: Bridge.refreshNow()
    }

    // ══════════════════════════════════════════════════════════════
    //  整体: 左侧导航 + 右侧内容
    // ══════════════════════════════════════════════════════════════
    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ══════════════════════════════════════════════════════════
        //  导航栏 (通高)
        // ══════════════════════════════════════════════════════════
        Rectangle {
            id: nav
            Layout.fillHeight: true
            Layout.preferredWidth: win.navExpanded ? Theme.navW : Theme.navWMin
            color: Theme.navBg
            Behavior on Layout.preferredWidth {
                NumberAnimation { duration: 150; easing.type: Easing.OutCubic }
            }

            Rectangle {
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: Theme.border
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // ── 顶部: 汉堡 + 标题 ──
                Rectangle {
                    Layout.fillWidth: true
                    height: 48
                    color: "transparent"

                    MouseArea {
                        id: menuBtn
                        width: 48
                        height: 48
                        anchors.left: parent.left
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: win.navExpanded = !win.navExpanded

                        NavIcon {
                            anchors.centerIn: parent
                            kind: "menu"
                            color: menuBtn.containsMouse ? Theme.textHi
                                                         : Theme.textMid
                        }
                    }

                    // ── 品牌图标 ──
                    // 用打包进 qrc 的官方 iCloud 标志(彩色渐变矢量)。
                    // 这里是全程序唯一使用彩色图标的位置 —— 品牌位就该是彩色的;
                    // 功能位一律用 NavIcon 的单色图元, 混色会显得突兀。
                    Image {
                        id: brandIcon
                        source: "qrc:/icons/icloud.svg"
                        // sourceSize 比逻辑尺寸大一倍, 高 DPI 下不糊
                        sourceSize: Qt.size(40, 40)
                        width: 20
                        height: 20
                        anchors.left: menuBtn.right
                        anchors.leftMargin: 2
                        anchors.verticalCenter: parent.verticalCenter
                        smooth: true
                        mipmap: true
                        // Image 加载失败不会报 QML 错误, 只会静默留白。
                        // 这里主动记一条, 免得换机器后图标不见了却查不到原因
                        // (缺 imageformats/qsvg.dll 是唯一可能的成因)。
                        onStatusChanged: if (status === Image.Error)
                                             console.warn("品牌图标加载失败(检查 qsvg 插件): "
                                                          + source)
                        // 折叠态整个导航只有 52px 宽, 放不下图标, 淡出
                        opacity: win.navExpanded ? 1 : 0
                        visible: opacity > 0
                        Behavior on opacity { NumberAnimation { duration: 120 } }
                    }

                    Label {
                        anchors.left: brandIcon.right
                        anchors.leftMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        text: "iCloud 监控"
                        color: Theme.textHi
                        font.pixelSize: Theme.fsBody
                        font.bold: true
                        opacity: win.navExpanded ? 1 : 0
                        visible: opacity > 0
                        Behavior on opacity { NumberAnimation { duration: 120 } }
                    }
                }

                // ── 首页(固定第一) ──
                NavItem {
                    Layout.fillWidth: true
                    text: "首页"
                    icon: "home"
                    collapsed: !win.navExpanded
                    selected: win.currentKey === "home"
                    onActivated: win.currentKey = "home"
                }

                // ── 中间四类(顺序可调, 未启用的不显示) ──
                Repeater {
                    model: win.visibleOrder

                    delegate: NavItem {
                        required property string modelData
                        Layout.fillWidth: true
                        text: Bridge.categoryName(modelData)
                        icon: win.iconFor(modelData)
                        collapsed: !win.navExpanded
                        selected: win.currentKey === modelData
                        onActivated: win.currentKey = modelData
                    }
                }

                // 设置固定最底 —— UWP NavigationView 的惯例
                Item { Layout.fillHeight: true }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    Layout.rightMargin: 8
                    height: 1
                    color: Theme.border
                }

                NavItem {
                    Layout.fillWidth: true
                    text: "设置"
                    icon: "settings"
                    collapsed: !win.navExpanded
                    selected: win.currentKey === "settings"
                    onActivated: win.currentKey = "settings"
                }

                Item { height: Theme.sp }
            }
        }

        // ══════════════════════════════════════════════════════════
        //  右侧: 命令栏 + 页面
        // ══════════════════════════════════════════════════════════
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                height: 48
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

                    Rectangle {
                        width: 10; height: 10; radius: 5
                        color: Theme.stateColor(Bridge.stateName)
                        Behavior on color { ColorAnimation { duration: 300 } }
                    }

                    Label {
                        text: Bridge.stateName
                        color: Bridge.dbOk ? Theme.stateColor(Bridge.stateName)
                                           : Theme.err
                        font.pixelSize: Theme.fsTitle
                        font.bold: true
                        // 与 StatusCard 同理: 状态名是变长文本(最长"双向传输"),
                        // 顶栏右侧是"重启/打开目录"操作按钮。给中间这段设
                        // 收缩上限, 窗口变窄时被截的是状态名而不是按钮。
                        Layout.maximumWidth: 140
                        elide: Text.ElideRight
                    }

                    Label {
                        text: Bridge.dbOk ? ("v" + Bridge.clientVersion)
                                          : "数据库不可读"
                        color: Theme.textLo
                        font.pixelSize: Theme.fsSmall
                    }

                    Item { Layout.fillWidth: true }

                    RowLayout {
                        spacing: 6

                        Label {
                            text: Bridge.icloudRunning ? "iCloud 运行中" : "iCloud 已停止"
                            color: Bridge.icloudRunning ? Theme.ok : Theme.err
                            font.pixelSize: Theme.fsSmall
                            font.bold: true
                        }

                        AppSwitch {
                            id: icloudSwitch
                            // ★ 开关状态完全由后端进程事实驱动。
                            //   **不要在这里写 `checked = ...`** ——
                            //   那是对绑定属性的 JS 赋值, 会把 `checked:
                            //   Bridge.icloudRunning` 这个绑定**删除掉**
                            //   (QML 规则: C++ 内部写属性不移除绑定, JS 赋值会)。
                            //   绑定一旦被删, checked 就永久停在用户最后拨到的
                            //   值, 不再跟随真实进程状态 —— 实测出现过
                            //   文字显示"iCloud 已停止"而开关仍停在"打开"。
                            //   正确做法: 只声明绑定, 点击时调后端动作,
                            //   让 onProcTimer 通过 icloudStateChanged 推回来。
                            checked: Bridge.icloudRunning
                            enabled: !Bridge.busyRestarting
                            onToggled: {
                                // 开关真的做"启停": 关->起 iCloud, 开->停 iCloud。
                                // 之前无论拨到哪边都是 restartIcloud(), 与旁边的
                                // "重启"按钮功能完全重复, 是个会误导人的假开关。
                                if (checked) Bridge.startIcloud()
                                else         Bridge.stopIcloud()
                            }
                            ToolTip.visible: hovered
                            ToolTip.text: Bridge.icloudRunning
                                          ? "点一下停止 iCloud"
                                          : "点一下启动 iCloud"
                            ToolTip.delay: 500
                        }

                        AppButton {
                            id: restartBtn
                            kind: "ghost"
                            minW: 72
                            text: Bridge.busyRestarting ? "重启中..." : "重启"
                            enabled: !Bridge.busyRestarting
                            onClicked: Bridge.restartIcloud()
                            ToolTip.visible: hovered
                            ToolTip.text: "结束全部 iCloud 进程后重新启动"
                            ToolTip.delay: 600
                        }
                    }

                    // 打开 iCloud 目录。
                    // 原来是 📂 emoji —— 彩色位图不受主题控制, 夹在一排单色图标里
                    // 最显眼, 这就是"打开云盘图标有点突兀"的来源。
                    // 改用 NavIcon 的 folder 图元: 与其它图标同一套线条、同一套
                    // 主题色, 尺寸也对齐按钮高度。
                    AppButton {
                        id: openDriveBtn
                        kind: "quiet"
                        iconKind: "folder"
                        minW: 34
                        padH: 8
                        onClicked: Bridge.openDriveRoot()
                        ToolTip.visible: hovered
                        ToolTip.text: "打开 iCloud 目录\n" + Bridge.driveRoot
                        ToolTip.delay: 600
                    }
                }
            }

            // ── 页面容器 ──
            // 子项顺序固定, 与侧边栏顺序无关(侧边栏顺序只影响导航显示)
            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: {
                    switch (win.currentKey) {
                    case "home":      return 0
                    case "drive":     return 1
                    case "photos":    return 2
                    case "passwords": return 3
                    case "bookmarks": return 4
                    case "settings":  return 5
                    }
                    return 0
                }

                HomePage {}
                DrivePage {}
                CategoryPage { catKey: "photos" }
                CategoryPage { catKey: "passwords" }
                CategoryPage { catKey: "bookmarks" }
                SettingsPage {}
            }
        }
    }

    // ══════════════════════════════════════════════════════════════
    //  Toast
    // ══════════════════════════════════════════════════════════════
    Rectangle {
        id: toast
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spL

        // 宽度用固定值, 不测 RowLayout.implicitWidth(否则绑定循环)
        readonly property int toastW: 360
        width: toastText.length > 0 ? toastW : 0
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
        Behavior on width {
            enabled: win.toastText.length > 0
            NumberAnimation { duration: 180 }
        }

        RowLayout {
            id: toastRow
            // ★ 不能用 anchors.fill, 也不能写 `height: implicitHeight`。
            //   toast.height 依赖 toastRow.implicitHeight, 而
            //   `height: implicitHeight` 对 ColumnLayout/RowLayout 自身就是
            //   **自引用**(implicitHeight 正是由自己的子项算出来的) ——
            //   于是 toast.height ← toastRow.implicitHeight
            //   ← toastRow.height(自引用) → Qt 判为绑定循环。
            //   实测报 `QML Flickable: Binding loop detected for
            //   property "contentHeight"`, 点开关弹 toast 时还会出现彩色撕裂。
            //   正确写法: 宽度取父宽(只水平), 高度完全不设 ——
            //   Layout 会自己按内容撑开。
            width: parent.width - Theme.sp * 2
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
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
                Layout.maximumWidth: 320
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: win.toastText = ""
        }
    }
}
