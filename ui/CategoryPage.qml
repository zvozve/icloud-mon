// ======================================================================
//  CategoryPage.qml  -  相册 / 密码 / 书签 三类共用的详情页
//
//  内容从 Bridge.categories 里按 catKey 取, 不写死任何一类 ——
//  加类别时只要数据层多输出一项, 这里不用改。
//
//  重要: 「是否启用」的自动检测对密码/书签**不可靠**(见 IcloudCategories.h),
//  所以页面上同时显示"生效值"和"自动检测依据", 让用户一眼看出当前值
//  是手动指定的还是自动推断的。
// ======================================================================
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: page

    // 由 Loader.onLoaded 或外部赋值: "photos" | "passwords" | "bookmarks"
    property string catKey: ""

    // 从全部类别里取出自己那一项。
    // 依赖 Bridge.categories 变化(每次采样刷新)自动重算。
    readonly property var cat: {
        var list = Bridge.categories
        for (var i = 0; i < list.length; ++i)
            if (list[i].key === page.catKey)
                return list[i]
        return null
    }
    readonly property bool isOn: cat !== null && cat.enabled === true

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
            // 留白用 spL(16), 与其它页一致。
            // 注意: 上一版这里误把 y 偏移整行删掉了(只剩注释), 于是 y=0,
            // 内容直接顶到页面最上沿 —— 那才是"顶部贴住"的真正原因。
            // 现在标题已包进带边框的面板容器(见下), 有明确边界, 16px 足够。
            // 留白的作用是"别贴边", 不是"越宽越好", 过大反而显得松散。
            y: Theme.spL
            spacing: Theme.spL

            // ── 标题行 ──
            //
            // ★ 为什么要包一层带边框的容器(而不是只靠 y 偏移留白):
            //   像素级实测(1085x850 截图扫描):
            //     书签页 y=48~78 全是 rgb(22,24,29) 页面背景, y=84 直接出现
            //     标题文字 —— 顶栏与标题之间 36px 全是空的深色背景,
            //     没有任何边界, 所以无论留白加到多少都"看着贴住"。
            //     云盘页则不同: y=72 就出现 rgb(45,50,60)(面板底色),
            //     因为它第一个元素是带边框的面板, 天然有视觉分隔。
            //   所以纯文字页(书签/密码/相册)的正确修法不是继续加大留白,
            //   而是给标题一个和云盘页一样的容器 —— 有了边框和底色,
            //   上下自然分开, 不再像贴着顶栏。
            Rectangle {
                Layout.fillWidth: true
                // ★ 原来写死 56, 装不下"20px 大字 + 上下各 16 边距"(需要 56+)。
                //   像素实测(1085x850 书签页, 扫描 x=225..330 逐行最亮像素):
                //     容器 y=16..71(高 56), 文字 y=37..56(高 20)
                //     → 上边距 21px, 下边距 15px —— **上下不对称**, 视觉上
                //       就是"文字贴着容器上沿"。
                //   改成 64: 上下各 22px, 对称, 大字也有呼吸空间。
                Layout.preferredHeight: 64
                color: Theme.bgPanel
                radius: Theme.radius
                border.color: Theme.border
                border.width: 1

                RowLayout {
                    // 边距用 spL(16), 高度 64 时上下各留 24, 上下对称。
                    anchors.fill: parent
                    anchors.margins: Theme.spL
                    spacing: Theme.sp

                    Rectangle {
                        width: 10; height: 10; radius: 5
                        Layout.alignment: Qt.AlignVCenter
                        color: page.isOn ? Theme.ok : Theme.textLo
                    }
                    Label {
                        text: page.cat ? page.cat.name : page.catKey
                        color: Theme.textHi
                        font.pixelSize: Theme.fsBig
                        font.bold: true
                    }
                    Rectangle {
                        visible: !page.isOn
                        Layout.alignment: Qt.AlignVCenter
                        width: 64; height: 22
                        radius: 11
                        color: Theme.bgInput
                        border.color: Theme.borderHi
                        border.width: 1
                        Text {
                            anchors.centerIn: parent
                            text: "未启用"
                            color: Theme.textLo
                            font.pixelSize: Theme.fsSmall
                        }
                    }
                    Item { Layout.fillWidth: true }
                }
            }

            // 未启用时给一条醒目的说明, 免得以为是"没数据"
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 44
                visible: !page.isOn
                color: Theme.bgInput
                radius: Theme.radius
                border.color: Theme.borderHi
                border.width: 1

                Label {
                    anchors.fill: parent
                    anchors.margins: Theme.sp
                    verticalAlignment: Text.AlignVCenter
                    text: "此类同步当前标记为未启用，下面的数据可能为空或不更新。"
                          + "启用状态可在「设置」里修改。"
                    color: Theme.textMid
                    font.pixelSize: Theme.fsSmall
                    wrapMode: Text.WordWrap
                }
            }

            // ── 摘要 ──
            Label {
                Layout.fillWidth: true
                text: page.cat ? page.cat.summary : ""
                color: Theme.textMid
                font.pixelSize: Theme.fsBody
                wrapMode: Text.WordWrap
            }

            // ── 明细 ──
            Rectangle {
                Layout.fillWidth: true
                // 高度交给内部 ColumnLayout 自撑: detailCol.implicitHeight 由真实的
                // 子项高度累加(含 spacing), 永远准确。
                // 用它的 implicitHeight **不会**成环 —— 前提是父矩形本身
                // 不设 height, 高度由外层 Layout 分配。写成
                // `Rectangle.height ← detailCol.implicitHeight ←
                //  detailCol.width ← Rectangle.width` 才成环。
                Layout.preferredHeight: detailCol.implicitHeight + Theme.sp * 2
                color: Theme.bgPanel
                radius: Theme.radius
                border.color: Theme.border
                border.width: 1

                ColumnLayout {
                    id: detailCol
                    // ★ 关键: 只用父宽; 高度不设(设 height: implicitHeight
                    //   是自引用, 会绑定循环)。
                    //   `anchors.fill: parent` 也不能用(与父高度成环)。
                    //
                    // ★★ 但**必须有垂直定位**! 之前只写了
                    //   anchors.horizontalCenter, 于是 detailCol 的 y 默认为 0,
                    //   内容从父矩形上沿开始画 —— 即使父矩形高度算对了,
                    //   第一行也会紧贴/压在上边框上(像素实测上边距仅 3px,
                    //   表现为"文字贴顶")。
                    //   水平居中 + 垂直居中: 上下各留 (父高-内容高)/2。
                    width: parent.width - Theme.sp * 2
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6

                    Repeater {
                        model: page.cat ? page.cat.details : []

                        delegate: RowLayout {
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true
                            spacing: Theme.sp

                            Label {
                                text: modelData.label
                                color: Theme.textLo
                                font.pixelSize: Theme.fsSmall
                                Layout.preferredWidth: 110
                            }
                            Label {
                                text: modelData.value
                                color: Theme.textMid
                                font.pixelSize: Theme.fsSmall
                                font.family: "Consolas, monospace"
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }
                        }
                    }
                }
            }

            // ── 启用状态来源 ──
            Rectangle {
                Layout.fillWidth: true
                // ★ 这块内部有 wrapMode: WordWrap 的 Label(第 2、3 行), 高度随
                //   文字换行数变化 —— 任何"行数 × 固定行高"的公式都算不准
                //   (之前写 srcRows * 20 + 12 就因为漏算 spacing 而偏小)。
                //   而且"启用状态来源"是固定标题, 另两行长度不定。
                //
                // 正确做法: 直接用内部 ColumnLayout 的 implicitHeight ——
                //   它由**真实子项**算出(Label 自己知道换了几行), 永远准。
                //   这里不会构成绑定循环: 前提是**父矩形不要设 height**,
                //   高度由 Layout 按 preferredHeight 分配给子项即可。
                //   (之前成环的写法是父矩形反过来取子项 implicitHeight:
                //    Rectangle.preferredHeight ← srcCol.implicitHeight
                //        ← srcCol.width ← Rectangle.width ← 同一矩形)
                //   现在方向相反 —— 子项报告需求, 父不给它回压。
                Layout.preferredHeight: srcCol.implicitHeight + Theme.sp * 2
                color: Theme.bgInput
                radius: Theme.radius
                border.color: Theme.border
                border.width: 1

                ColumnLayout {
                    id: srcCol
                    // 同 detailCol: 只用父宽 + 垂直居中。
                    // ★ 必须有垂直定位 —— 只写 horizontalCenter 时 y 默认 0,
                    //   内容会压在上边框上(实测上边距仅 3px)。
                    width: parent.width - Theme.sp * 2
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4

                    Label {
                        text: "启用状态来源"
                        color: Theme.textHi
                        font.pixelSize: Theme.fsSmall
                        font.bold: true
                    }
                    Label {
                        Layout.fillWidth: true
                        text: page.cat && page.cat.userSet === true
                              ? "手动指定：" + (page.cat.enabled ? "已启用" : "未启用")
                              : "自动检测：" + (page.cat && page.cat.autoOn
                                                ? "判定为已启用" : "判定为未启用")
                                + "（尚未手动指定）"
                        color: Theme.textMid
                        font.pixelSize: Theme.fsSmall
                        wrapMode: Text.WordWrap
                    }
                    Label {
                        Layout.fillWidth: true
                        text: "自动检测依据：" + (page.cat ? page.cat.autoWhy : "—")
                        color: Theme.textLo
                        font.pixelSize: Theme.fsTiny
                        font.family: "Consolas, monospace"
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // ── 补充说明 ──
            Label {
                Layout.fillWidth: true
                visible: page.cat && page.cat.note !== ""
                text: page.cat ? page.cat.note : ""
                color: Theme.textLo
                font.pixelSize: Theme.fsTiny
                wrapMode: Text.WordWrap
            }

            Item { Layout.preferredHeight: Theme.sp }
        }
    }
}
