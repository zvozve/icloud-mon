// ======================================================================
//  SettingsPage.qml  -  设置页（独立页面，不再是滑出面板）
//
//  改造自原来的覆盖层版本, 去掉了:
//    - 半透明遮罩 + MouseArea 点击关闭
//    - 滑入/滑出动画与面板圆角、关闭按钮
//  因为现在由左侧导航切页, 不再需要"弹出再收起"这层交互。
//
//  内容是竖长的一列, 所以包在 Flickable 里; 并限宽居中
//  (UWP 设置页就是这个做法, 一行拉太长不好读)。
// ======================================================================
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Dialogs 1.3
import QtQuick.Layouts 1.15

Item {
    id: page

    // 默认启动页下拉的模型: 首页 + 中间四类(按当前侧边栏顺序, 只列已启用的)
    // + 设置。顺序与侧边栏保持一致, 否则下拉里的顺序和实际导航对不上;
    // 未启用的类别在侧边栏已隐藏, 这里也不再列为可选启动页。
    readonly property var pageModel: {
        // ★ 必须返回**字符串数组**(页面名), 不能是 {key,name} 对象数组。
        //
        //   原来返回 [{key:"home",name:"首页"}, ...] 并配合 textRole:"name"。
        //   但 ComboBox 的 displayText 是在 **C++ 侧**读的 model —— 对 QML
        //   的 JS 数组, 它取不到对象的 "name" 字段, 于是关闭态显示的是
        //   错的文字(实测显示"云盘"但那一项与实际选择对不上)。
        //   字符串数组是 ComboBox 的原生 model 类型, 显示绝对正确;
        //   需要 key 时用 pageKeys(下标一一对应)。
        var cats = Bridge.categories
        var m = [ "首页" ]
        var o = Bridge.navOrder
        for (var i = 0; i < o.length; ++i) {
            var enabled = true
            for (var j = 0; j < cats.length; ++j)
                if (cats[j].key === o[i]) { enabled = cats[j].enabled === true; break }
            // 只列**已启用**的类别: 未启用的在侧边栏和主页都隐藏了,
            // 这里列出来会让用户选到一个打不开的页面。
            if (enabled) m.push(Bridge.categoryName(o[i]))
        }
        m.push("设置")
        return m
    }

    // 与 pageModel 下标一一对应的 key 数组(首页固定在 0, 设置固定在末尾)
    readonly property var pageKeys: {
        var cats = Bridge.categories
        var k = [ "home" ]
        var o = Bridge.navOrder
        for (var i = 0; i < o.length; ++i) {
            var enabled = true
            for (var j = 0; j < cats.length; ++j)
                if (cats[j].key === o[i]) { enabled = cats[j].enabled === true; break }
            if (enabled) k.push(o[i])
        }
        k.push("settings")
        return k
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
            // 限宽并居中。这里没有 fillHeight 子项, 所以不必给确定高度。
            width: Math.min(body.width - Theme.spL * 2, 640)
            x: (body.width - width) / 2
            y: Theme.spL
            spacing: Theme.spL

            Label {
                text: "设置"
                color: Theme.textHi
                font.pixelSize: Theme.fsBig
                font.bold: true
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

            // ══════════════════════════════════════════════════
            //  开机自启动
            // ══════════════════════════════════════════════════
            RowLayout {
                Layout.fillWidth: true

                ColumnLayout {
                    spacing: 1
                    Layout.fillWidth: true
                    Label {
                        text: "开机自启动"
                        color: Theme.textHi
                        font.pixelSize: Theme.fsBody
                        font.bold: true
                    }
                    Label {
                        text: "登录 Windows 后自动运行本监控程序"
                        color: Theme.textLo
                        font.pixelSize: Theme.fsSmall
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }

                AppSwitch {
                    id: autoStartSwitch
                    checked: Bridge.autoStart
                    onToggled: {
                        // 后端写注册表可能失败(如权限), 失败时需要回滚 UI。
                        //
                        // ★ 但**不能写 `checked = Bridge.autoStart`**:
                        //   那是对绑定属性的 JS 赋值, 会把上面
                        //   `checked: Bridge.autoStart` 这个绑定删除掉,
                        //   之后开关就永远停在最后一次手动拨到的值,
                        //   不再跟随真实状态(注册表被外部改动也感知不到)。
                        //   QML 规则: C++ 内部写属性不移除绑定, JS 赋值会。
                        //   正确做法: 调后端, 由后端在失败时 emit
                        //   autoStartChanged 把真值推回来, 绑定自然复位。
                        if (!Bridge.setAutoStart(checked))
                            console.warn("[Settings] 开机自启动设置失败, 等待后端回推真值")
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

            // ══════════════════════════════════════════════════
            //  刷新间隔
            // ══════════════════════════════════════════════════
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: "刷新间隔"
                        color: Theme.textHi
                        font.pixelSize: Theme.fsBody
                        font.bold: true
                        Layout.fillWidth: true
                    }
                    Label {
                        text: Bridge.refreshSec + " 秒"
                        color: Theme.info
                        font.pixelSize: Theme.fsBody
                        font.family: "Consolas, monospace"
                    }
                }

                AppSlider {
                    id: refreshSlider
                    Layout.fillWidth: true
                    accent: Theme.info
                    from: 1
                    to: 120
                    stepSize: 1
                    value: Bridge.refreshSec
                    onMoved: Bridge.refreshSec = Math.round(value)

                    // 间隔越大越省资源, 但反应越慢
                    readonly property string hint: value <= 2 ? "实时(较耗资源)"
                        : value <= 10 ? "推荐"
                        : value <= 30 ? "省资源" : "很省资源(反应慢)"
                }

                Label {
                    text: refreshSlider.hint
                    color: Theme.textLo
                    font.pixelSize: Theme.fsTiny
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

            // ══════════════════════════════════════════════════
            //  卡死阈值
            // ══════════════════════════════════════════════════
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: "卡死判定阈值"
                        color: Theme.textHi
                        font.pixelSize: Theme.fsBody
                        font.bold: true
                        Layout.fillWidth: true
                    }
                    Label {
                        text: Bridge.stallSec + " 秒"
                        color: Theme.warn
                        font.pixelSize: Theme.fsBody
                        font.family: "Consolas, monospace"
                    }
                }

                AppSlider {
                    id: stallSlider
                    Layout.fillWidth: true
                    accent: Theme.warn
                    from: 30
                    to: 1800
                    stepSize: 30
                    value: Bridge.stallSec
                    onMoved: Bridge.stallSec = Math.round(value / 30) * 30

                    readonly property string hint:
                        value <= 120 ? "灵敏(易误报)"
                        : value <= 420 ? "推荐, 5 分钟内无变化即告警"
                        : "迟钝"
                }

                Label {
                    text: stallSlider.hint
                    color: Theme.textLo
                    font.pixelSize: Theme.fsTiny
                }

                Label {
                    text: "判定规则: 有任务在跑, 但队列计数持续不变 = 卡死。空闲不判定。"
                    color: Theme.textLo
                    font.pixelSize: Theme.fsTiny
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

            // ══════════════════════════════════════════════════
            //  同步根目录
            // ══════════════════════════════════════════════════
            RowLayout {
                Layout.fillWidth: true

                ColumnLayout {
                    spacing: 1
                    Layout.fillWidth: true
                    Label {
                        text: "同步根目录"
                        color: Theme.textHi
                        font.pixelSize: Theme.fsBody
                        font.bold: true
                    }
                    Label {
                        text: "点「浏览」选择 iCloud Drive 本地文件夹"
                        color: Theme.textLo
                        font.pixelSize: Theme.fsSmall
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    // 当前值走 Bridge 属性而非 DriveRoot 上下文属性 ——
                    // 后者是启动时的快照, 改完不会刷新。
                    Label {
                        text: Bridge.driveRoot
                        color: Theme.info
                        font.pixelSize: Theme.fsTiny
                        font.family: "Consolas, monospace"
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                }

                AppButton {
                    id: browseDriveBtn
                    kind: "ghost"
                    text: "浏览"
                    onClicked: driveDialog.open()
                }

                AppButton {
                    id: resetDriveBtn
                    kind: "ghost"
                    text: "默认"
                    visible: Bridge.driveRoot !== "C:/Container/Cloud/iCloudDrive"
                    onClicked: Bridge.resetDriveRoot()
                    ToolTip.visible: hovered
                    ToolTip.text: "恢复内置默认路径"
                    ToolTip.delay: 600
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

            // ══════════════════════════════════════════════════
            //  运行日志
            // ══════════════════════════════════════════════════
            RowLayout {
                Layout.fillWidth: true

                ColumnLayout {
                    spacing: 1
                    Layout.fillWidth: true
                    Label {
                        text: "运行日志"
                        color: Theme.textHi
                        font.pixelSize: Theme.fsBody
                        font.bold: true
                    }
                    Label {
                        text: "按天记录同步变化、运行状态与操作记录"
                        color: Theme.textLo
                        font.pixelSize: Theme.fsSmall
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Label {
                        text: Bridge.logFile
                        color: Theme.textLo
                        font.pixelSize: Theme.fsTiny
                        font.family: "Consolas, monospace"
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                }

                AppButton {
                    id: logBtn
                    kind: "ghost"
                    text: "打开"
                    onClicked: Bridge.openLogDir()
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

            // ══════════════════════════════════════════════════
            //  系统通知
            // ══════════════════════════════════════════════════
            RowLayout {
                Layout.fillWidth: true

                ColumnLayout {
                    spacing: 1
                    Layout.fillWidth: true
                    Label {
                        text: "系统通知"
                        color: Theme.textHi
                        font.pixelSize: Theme.fsBody
                        font.bold: true
                    }
                    Label {
                        text: "iCloud 卡死时弹 Windows 通知，窗口最小化也能收到"
                        color: Theme.textLo
                        font.pixelSize: Theme.fsSmall
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }

                AppButton {
                    id: testNotifyBtn
                    kind: "ghost"
                    text: "测试通知"
                    onClicked: Bridge.testSystemNotification()
                    ToolTip.visible: hovered
                    ToolTip.text: "立即弹一条测试通知，确认系统通知是否正常"
                    ToolTip.delay: 600
                }
            }

            // ══════════════════════════════════════════════════
            //  类别启用状态（手动指定）
            // ══════════════════════════════════════════════════
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                Label {
                    text: "类别启用状态"
                    color: Theme.textHi
                    font.pixelSize: Theme.fsBody
                    font.bold: true
                }
                Label {
                    Layout.fillWidth: true
                    text: "自动检测只对云盘和相册可靠；密码和书签检测不出来"
                          + "（真正的开关存在被 iCloud 独占锁定的配置里）。"
                          + "请在下面手动指定，点「自动」可恢复为自动检测。"
                    color: Theme.textLo
                    font.pixelSize: Theme.fsTiny
                    wrapMode: Text.WordWrap
                }

                Repeater {
                    model: Bridge.categories

                    delegate: RowLayout {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        spacing: Theme.sp

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Label {
                                text: modelData.name
                                      + (modelData.userSet ? "（手动指定）"
                                                           : "（自动检测）")
                                color: Theme.textHi
                                font.pixelSize: Theme.fsSmall
                                font.bold: true
                            }
                            Label {
                                Layout.fillWidth: true
                                text: "自动检测："
                                      + (modelData.autoOn ? "已启用" : "未启用")
                                      + " · " + modelData.autoWhy
                                color: Theme.textLo
                                font.pixelSize: Theme.fsTiny
                                elide: Text.ElideRight
                            }
                        }

                        AppButton {
                            id: autoBtn
                            kind: "ghost"
                            text: "自动"
                            enabled: modelData.userSet === true
                            onClicked: Bridge.clearCategoryEnabled(modelData.key)
                            ToolTip.visible: hovered
                            ToolTip.text: "清除手动指定，回退到自动检测"
                            ToolTip.delay: 600
                        }

                        AppSwitch {
                            checked: modelData.enabled === true
                            onToggled: Bridge.setCategoryEnabled(modelData.key,
                                                                 checked)
                        }
                    }
                }
            }

            // ══════════════════════════════════════════════════
            //  侧边栏顺序
            // ══════════════════════════════════════════════════
            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: "侧边栏顺序"
                        color: Theme.textHi
                        font.pixelSize: Theme.fsBody
                        font.bold: true
                        Layout.fillWidth: true
                    }
                    AppButton {
                        id: resetOrderBtn
                        kind: "ghost"
                        text: "恢复默认"
                        onClicked: Bridge.resetNavOrder()
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: "首页固定在第一位、设置固定在最后一位，都不可调；"
                          + "中间四类按住行上下拖动即可排序，也可用 ↑ ↓ 精确移动。"
                    color: Theme.textLo
                    font.pixelSize: Theme.fsTiny
                    wrapMode: Text.WordWrap
                }

                // ── 拖拽排序列表 ──
                //
                // 用 ListView 而非 Repeater: 拖拽需要 ListView 提供
                // itemAt(y)/indexAt(y) 来判断"拖到了哪一行"。
                //
                // 交互: 按住整行上下拖动松手即排序(主方式),
                //       右侧 ↑↓ 保留为精确的单步移动(辅助)。
                // m_navOrder 只含中间四类, 行号与 ListView 的 index 一一对应,
                // 所以 drop 时直接把 index 传给 Bridge.moveCategoryTo。
                ListView {
                    id: orderList
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.max(1, Bridge.navOrder.length) * 40
                    clip: true
                    model: Bridge.navOrder
                    boundsBehavior: Flickable.StopAtBounds

                    // 被拖动的那一行: 跟随手指 + 半透明 + 提亮
                    property int dragIndex: -1
                    property int dropIndex: -1

                    ScrollBar.vertical: ScrollBar {}

                    delegate: ItemDelegate {
                        id: orderRow
                        required property string modelData
                        required property int index
                        width: orderList.width
                        height: 40

                        // 拖到目标位置时, 显示一条插入指示线
                        readonly property bool isDropTarget:
                            orderList.dragIndex >= 0
                            && orderList.dropIndex === index
                            && orderList.dragIndex !== index

                        // 正在被拖的那一行: 提亮 + 半透明 + 抬层级, 让用户
                        // 明确看到"这一行正在被拖"。不做跟随手指的位移 ——
                        // 那是 delegate 的 y, 由 ListView 管, 改会和布局打架。
                        readonly property bool isDragging:
                            orderList.dragIndex === index

                        z: isDragging ? 10 : 0
                        opacity: isDragging ? 0.7 : 1.0
                        scale: isDragging ? 1.02 : 1.0
                        Behavior on opacity { NumberAnimation { duration: 90 } }
                        Behavior on scale { NumberAnimation { duration: 90 } }

                        contentItem: RowLayout {
                            spacing: 8

                            // 拖动把手
                            Text {
                                text: "\u2630"          // ☰
                                color: Theme.textLo
                                font.pixelSize: Theme.fsBody
                                Layout.alignment: Qt.AlignVCenter
                            }

                            Label {
                                text: (index + 1) + ".  "
                                      + Bridge.categoryName(orderRow.modelData)
                                color: Theme.textMid
                                font.pixelSize: Theme.fsSmall
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                elide: Text.ElideRight
                                Layout.alignment: Qt.AlignVCenter
                            }
                        }

                        background: Rectangle {
                            color: orderList.dragIndex === orderRow.index
                                   ? Theme.bgHover : "transparent"
                            radius: 3
                            Behavior on color { ColorAnimation { duration: 100 } }
                        }

                        // 插入指示线: 拖到本行位置时显示在顶部
                        Rectangle {
                            visible: orderRow.isDropTarget
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.right: parent.right
                            height: 2
                            color: Theme.info
                        }

                        // 整行按下即进入拖拽
                        MouseArea {
                            id: dragArea
                            anchors.fill: parent
                            drag.target: null
                            drag.axis: Drag.YAxis
                            drag.minimumY: 0
                            drag.maximumY: orderList.height - height

                            onPressed: {
                                pressY = mouseY
                                pressIndex = orderRow.index
                                orderList.dragIndex = orderRow.index
                                orderList.dropIndex = orderRow.index
                            }
                            onReleased: {
                                const from = orderList.dragIndex
                                const to = orderList.dropIndex
                                // 先清状态再提交: moveCategoryTo 会 emit
                                // navOrderChanged 让整个 ListView 重建, 此时
                                // dragIndex 若还指着已销毁的旧行, 下一行代理
                                // 初始化时会读到脏值。
                                orderList.dragIndex = -1
                                orderList.dropIndex = -1
                                if (from >= 0 && to >= 0 && to !== from)
                                    Bridge.moveCategoryTo(orderRow.modelData, to)
                            }
                            // ★ 用**位移量**算落点, 不再用 indexAt 反推。
                            //
                            // 原来靠 `orderList.indexAt(0, mapToItem 出来的 y)`
                            // 反推行号, 三个问题叠加, 结果是"拖拽完全没用":
                            //   ① indexAt 少传参数时抛 "Insufficient arguments",
                            //      异常在 onPositionChanged 里反复触发(日志实测
                            //      171 次), dropIndex 一次都没更新成功;
                            //   ② 指针落在两行间隙时返回 -1, 兜底又退回本行
                            //      -> dropIndex 恒等于 index, 松手直接 return;
                            //   ③ 即便参数补对, 这条路径仍依赖 indexAt 的
                            //      边界语义, 不够直接。
                            // 现在记住按下位置, 按位移量折算成"移动了几行",
                            // 再夹到合法范围。纯算术, 不查坐标, 无间隙问题。
                            property real pressY: 0
                            property int pressIndex: -1

                            onPositionChanged: {
                                if (orderList.dragIndex !== orderRow.index)
                                    return
                                const moved = Math.round(
                                    (mouseY - pressY) / orderRow.height)
                                const target = pressIndex + moved
                                orderList.dropIndex = Math.max(
                                    0, Math.min(orderList.count - 1, target))
                            }
                        }
                    }
                }
            }

            // ══════════════════════════════════════════════════
            //  默认启动页
            // ══════════════════════════════════════════════════
            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

            RowLayout {
                Layout.fillWidth: true

                ColumnLayout {
                    spacing: 1
                    Layout.fillWidth: true
                    Label {
                        text: "默认启动页"
                        color: Theme.textHi
                        font.pixelSize: Theme.fsBody
                        font.bold: true
                    }
                    Label {
                        text: "下次启动时打开哪一页"
                        color: Theme.textLo
                        font.pixelSize: Theme.fsSmall
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }

                // 原来用裸 ComboBox, 走 Fusion 浅色默认皮肤, 与深色主题完全不搭。
    // 换统一的 AppComboBox(背景/文字/箭头/下拉列表全部走 Theme)。
    AppComboBox {
                    id: defaultPageBox
                    Layout.preferredWidth: 160
                    // model 现在是**字符串数组**(页面名), ComboBox 直接显示,
                    // 不需要 textRole —— 之前用 textRole 配对象数组是显示错误
                    // 的根源(C++ 侧读不到 JS 对象的 name 字段)。
                    model: page.pageModel
                    currentIndex: {
                        var k = page.pageKeys
                        for (var i = 0; i < k.length; ++i)
                            if (k[i] === Bridge.defaultPage)
                                return i
                        return 0
                    }
                    // 下标对应 pageKeys, 由它取 key 写回。
                    onActivated: Bridge.defaultPage = page.pageKeys[index]
                }
            }

            // ══════════════════════════════════════════════════
            //  路径信息 (只读)
            // ══════════════════════════════════════════════════
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: pathCol.implicitHeight + 12
                color: Theme.bgInput
                radius: 3
                border.color: Theme.border
                border.width: 1

                // ★ pathCol 不用 anchors.fill(只用父宽), 高度也**不能**写
                //   `height: implicitHeight` —— 那是自引用(implicitHeight
                //   由自己的子项算出), 会构成
                //   父矩形高 ← pathCol.implicitHeight ← pathCol.height(自引用)
                //   的绑定循环, 实测报 contentHeight Binding loop。
                //   高度完全不设, 让 Layout 自己按内容撑开。
                ColumnLayout {
                    id: pathCol
                    width: parent.width - 12
                    anchors.horizontalCenter: parent.horizontalCenter
                    // ★ 同 CategoryPage.detailCol: 只写 horizontalCenter 时
                    //   y 默认 0, 内容会压在上边框上(实测上边距仅 3px)。
                    //   必须补垂直居中, 上下才都有留白。
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2

                    Label {
                        text: "同步根: " + Bridge.driveRoot
                        color: Theme.textLo
                        font.pixelSize: Theme.fsTiny
                        font.family: "Consolas, monospace"
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                    Label {
                        text: "历史: " + Bridge.stateFilePath()
                        color: Theme.textLo
                        font.pixelSize: Theme.fsTiny
                        font.family: "Consolas, monospace"
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                    Label {
                        text: "版本: " + AppVersion + "  |  iCloud " + Bridge.clientVersion
                        color: Theme.textLo
                        font.pixelSize: Theme.fsTiny
                    }
                }
            }

            // ══════════════════════════════════════════════════
            //  项目仓库 (点击用默认浏览器打开)
            // ══════════════════════════════════════════════════
            Rectangle {
                id: repoBox
                Layout.fillWidth: true
                // ★ 固定高度, 不用 repoRow.implicitHeight 推导。
                //   之前写成 anchors.fill 的 RowLayout + 父矩形高度依赖其
                //   implicitHeight, 形成循环: 父.height ← row.implicitHeight
                //   ← (row anchors.fill 父) ← 父.height。点击开关触发重算时
                //   Qt 报 "Binding loop detected for contentHeight",
                //   表现为界面出现随机彩色线条(渲染撕裂)。这里断开依赖。
                Layout.preferredHeight: 28
                color: Theme.bgInput
                radius: 3
                border.color: repoMouse.containsMouse ? Theme.info : Theme.border
                border.width: 1
                Behavior on border.color { ColorAnimation { duration: 120 } }

                Label {
                    id: repoTitle
                    anchors.left: parent.left
                    anchors.leftMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    text: "项目仓库"
                    color: Theme.textLo
                    font.pixelSize: Theme.fsTiny
                }

                Label {
                    id: repoLink
                    // 仓库地址单点来源, 换仓库改这一处即可
                    readonly property string url: "https://github.com/zvozve/icloud-mon"
                    anchors.left: repoTitle.right
                    anchors.leftMargin: Theme.sp
                    anchors.right: parent.right
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    text: url
                    color: repoMouse.containsMouse ? Theme.infoHi : Theme.info
                    font.pixelSize: Theme.fsTiny
                    font.underline: repoMouse.containsMouse
                    elide: Text.ElideRight
                }

                MouseArea {
                    id: repoMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: Bridge.openUrl(repoLink.url)
                }
            }

            Item { Layout.preferredHeight: Theme.spL }
        }

        // ── 目录选择对话框 ──
        // 注意: QtQuick.Dialogs 的 FileDialog 用 selectFolder 选目录,
        // 没有 folderMode 属性 (那是 Qt.labs.platform 的 API, 两者不同)。
        FileDialog {
            id: driveDialog
            title: "选择 iCloud Drive 目录"
            selectFolder: true
            // 从当前路径打开, 避免每次都从盘符根开始翻
            folder: "file:///" + Bridge.driveRoot
            onAccepted: Bridge.setDriveRoot(selectedFile.toString())
        }
    }
}
