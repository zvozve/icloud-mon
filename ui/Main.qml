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

    // ★ 窗口尺寸: **默认就等于最小尺寸**(860x620)。
    //
    //   原来是 width:1100 / height:840, 而 minimumWidth:860 /
    //   minimumHeight:620 —— 于是存在一块"最小尺寸到默认尺寸之间"的状态,
    //   界面上从没为它设计过: 860~1100 宽度下的布局是这几轮修布局时
    //   反复出问题的地方(按钮被裁/文字贴边/内容溢出), 靠 minimumWidth
    //   兜底并不等于"在这个尺寸下好看"。
    //
    //   既然如此, 默认就取最小尺寸:
    //     - 小屏/笔记本用户一启动就看到完整布局, 不必手动拉大;
    //     - 避开那块"没设计过的中间尺寸区间";
    //     - 默认尺寸 == minimumWidth/Height, 只有用户主动拖大才会有更大布局。
    //
    //   用 readonly property 单一来源, 避免 width/minimumWidth 两处
    //   各写各的以后又走偏。
    readonly property int defaultW: 860
    readonly property int defaultH: 620
    width: defaultW
    height: defaultH
    minimumWidth: defaultW
    minimumHeight: defaultH
    visible: true
    // 标题栏: 应用名 + 版本 + 当前页名。
    // AppVersion 由 main.cpp 注入(= CMake project VERSION)。
    //
    // ★ 原来标题里带了 Bridge.stateName(如"iCloud 状态监控 v1.0.1 - 已同步"),
    //   那也是一处状态显示。既然状态只在云盘页的状态卡里展示(顶栏已删),
    //   标题栏就跟着去掉 —— 标题栏是**所有页面共用**的框架, 在书签页的
    //   标题里写"已同步"同样是噪音。改用 pageTitle(当前页名), 与顶栏
    //   左侧显示的是同一个信息, 两处一致而不是各说各话。
    title: "iCloud 状态监控 v" + AppVersion
           + (pageTitle !== "" ? " - " + pageTitle : "")
    color: Theme.bgRoot

    // 导航栏是否展开(仅图标 <-> 图标+文字)
    property bool navExpanded: true
    // 当前页面 key: home | drive | photos | passwords | bookmarks | settings
    property string currentKey: "home"

    // 当前页面的中文名, 给顶栏与窗口标题共用。
    // 从 Bridge.categories 取(它同时是侧边栏的数据源), 所以未启用的
    // 类别被隐藏时这里也拿不到名字, 不会出现"显示着书签但侧边栏没有书签"。
    readonly property string pageTitle: {
        switch (currentKey) {
        case "home":      return "首页"
        case "drive":     return "云盘"
        case "settings":  return "设置"
        }
        for (var i = 0; i < Bridge.categories.length; ++i)
            if (Bridge.categories[i].key === currentKey)
                return Bridge.categories[i].name
        return ""
    }

    // 启动时按用户设置的默认启动页定位
    Component.onCompleted: currentKey = Bridge.defaultPage

    // ══════════════════════════════════════════════════════════════
    //  标题栏关闭按钮的行为(设置页可切换)
    //
    //  · Bridge.closeToTray == true  -> **关闭窗口**, 进程留在后台常驻
    //    (继续监控, 托盘图标保留作为唯一入口);
    //  · Bridge.closeToTray == false -> 关闭 = 退出整个程序。
    //
    //  ★ 这里是"关窗口"不是"最小化": hide() 之后窗口从屏幕**和任务栏**
    //    一起消失, 不会变成任务栏上的一个最小化按钮 —— 符合"关闭窗口但
    //    后台常驻"的语义。
    //
    //  ★ main.cpp 里设了 setQuitOnLastWindowClosed(false)。不设的话,
    //    hide() 掉最后一个可见窗口会被 Qt 当成"窗口都关了"而直接结束
    //    进程 —— "后台常驻"就退化成"点关闭就退出", 开关形同虚设。
    //    代价是两种情况都必须在这里**显式**处理, 不能再依赖 Qt 的默认退出。
    //
    //  ★ 关闭时不能用窗口内的提示条提示 —— 窗口已经隐藏, 弹给自己看没意义。
    //    走 Windows 系统通知(Bridge.notifySystem -> SystemNotifier): 窗口
    //    隐藏/最小化时它照样出现在屏幕右下角, 并留在通知中心里。
    // ══════════════════════════════════════════════════════════════
    onClosing: function(close) {
        if (Bridge.closeToTray) {
            close.accepted = false        // 拦截本次关闭, 改为隐藏窗口
            win.hide()
            Bridge.notifySystem("iCloud 监控仍在后台运行",
                                "窗口已关闭, 程序继续在后台监控。"
                                + "双击托盘图标可重新打开窗口, 右键托盘图标可彻底退出。")
        } else {
            close.accepted = true
            Qt.quit()
        }
    }

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

    // ── 窗口内提示条状态 ──
    // 它管两件事: ①操作反馈(开关/重启/定位失败等)
    //            ②**系统通知的窗口内同步提醒**(见下面的 onSystemNotify)
    property string toastText: ""
    property string toastKind: "info"
    property string toastTitle: ""

    function showToast(kind, text, title) {
        toastKind = kind
        toastText = text
        toastTitle = title === undefined ? "" : title
        toastTimer.restart()
    }

    Connections {
        target: Bridge
        function onNotify(kind, text) {
            win.showToast(kind, text)
        }
        // 系统通知(卡死告警 / 测试通知 / 关窗后台常驻提示)
        //  ★ 窗口在前台 -> 同步再弹一条窗口内提醒: 人正看着窗口, 这条才是
        //    一定能看见的那份(系统通知可能被"专注助手/勿扰"吞掉)。
        //  ★ 系统通道不可用(用户在 Windows 设置里关掉了本应用的通知)
        //    -> 无条件补弹, 否则这条告警就彻底没人看得见了。
        function onSystemNotify(title, detail) {
            if (win.active || !Bridge.systemNotifyAvailable)
                win.showToast("system", detail, title)
        }
        function onIcloudStateChanged() {
            win.ipcTimer.restart()
        }
    }

    Timer {
        id: toastTimer
        // 系统级告警停留久一点, 普通操作反馈短一点
        interval: win.toastKind === "system" ? 8000 : 4000
        onTriggered: { win.toastText = ""; win.toastTitle = "" }
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

            // ★ 顶部固定栏已整块删除。
            //   原来这里是 48px 高的深色固定栏, 内容: 状态圆点+状态名 /
            //   iCloud 版本 / 启停开关 / 打开目录图标。全部搬走了:
            //     - 同步状态 -> 云盘页 StatusCard(唯一权威位置)
            //     - 启停开关 -> 首页(全局操作放入口页最自然)
            //     - 打开目录 -> 云盘页 StatusCard 右侧(与游标/进程同上下文)
            //     - 版本号  -> 首页 iCloud 控制区右侧
            //   固定栏挂在每个页面上, 但这些内容大多只对某一页有意义
            //   (同步状态/打开目录只对云盘页), 在书签页顶栏也摆一遍
            //   就是噪音。删掉后内容区直接顶到窗口上沿, 更省垂直空间。

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
    //  窗口内提示条(操作反馈 + 系统通知的前台副本)
    //
    //  ★ 系统通知本体走 Windows 通知中心(见 inc/SystemNotifier.h);
    //    这里只是"窗口正好在前台"时多弹一份 —— 那种时候人看的是窗口。
    //    窗口隐藏或不在前台时不弹: 系统通知已经在屏幕右下角了, 再弹一份
    //    就是同一件事说两遍。
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
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 5
            }
            // 标题(可选, 只有系统通知带) + 正文
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Label {
                    visible: win.toastTitle !== ""
                    text: win.toastTitle
                    color: Theme.textHi
                    font.pixelSize: Theme.fsSmall
                    font.bold: true
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Label {
                    text: win.toastText
                    color: win.toastTitle !== "" ? Theme.textMid : Theme.textHi
                    font.pixelSize: Theme.fsSmall
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: { win.toastText = ""; win.toastTitle = "" }
        }
    }
}
