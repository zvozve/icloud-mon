// ======================================================================
//  StatusCard.qml  -  同步状态卡(只管同步)
//
//  显示: 状态名 + 详情 + 最近错误 + 操作按钮
//
// ★ 本卡只保留**同步**相关的内容。进程列表、同步游标、卡死告警都已
//   搬到首页 —— 它们描述的是"软件/iCloud 进程运行得怎么样", 属于软件
//   状态, 不是云盘内容。放在云盘页会让人以为只有打开云盘页才能看到
//   程序是否正常。
// ======================================================================
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root
    // 进程/游标搬走后, 卡片只剩"状态名 + 详情 + 按钮"。
    // 高度按内容算: 上下边距 spL*2 + 状态名行(12px 圆点撑起约 30)
    // + 详情行(约 17)。写 72 时详情行底部会被卡片下边缘压到,
    // 改成 80 留足呼吸。
    implicitHeight: 80
    color: Theme.bgPanel
    radius: Theme.radius
    border.width: 1
    border.color: Theme.border

    // 窗口缩到最小宽度时, 卡片内容可能比可视区宽。
    // 不裁的话, 右侧的"刷新/更新时间"会画到卡片外面、被页面边界切掉
    // (表现为按钮只剩半个字)。这里显式裁剪兜底 —— 内容超宽时宁可少显示,
    // 也不能让控件溢出容器。
    clip: true

    // ★ 这里原来会 `border.color: Bridge.anomalous ? Theme.err : ...`
    //   (卡死时边框转红)。已去掉: 卡死告警连同进程列表一起搬到首页了,
    //   这张卡只表达同步状态, 不该被"程序异常"染色。

    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.spL
        spacing: Theme.spL

        // ── 状态圆点 + 名称 ──
        ColumnLayout {
            spacing: 2
            Layout.alignment: Qt.AlignVCenter
            Layout.minimumWidth: 0

            RowLayout {
                spacing: 8
                Rectangle {
                    width: 12; height: 12; radius: 6
                    color: Theme.stateColor(Bridge.stateName)
                    Behavior on color { ColorAnimation { duration: 300 } }
                }
                Label {
                    text: Bridge.stateName
                    color: Bridge.dbOk ? Theme.stateColor(Bridge.stateName)
                                       : Theme.err
                    font.pixelSize: Theme.fsHuge
                    font.bold: true
                    // 状态名是变长文本(最长"双向传输"), 右侧是操作按钮。
                    // 给收缩上限, 窗口变窄时被截的是状态名而不是按钮。
                    Layout.maximumWidth: 220
                    elide: Text.ElideRight
                }
            }

            Label {
                text: Bridge.stateDetail
                color: Theme.textMid
                font.pixelSize: Theme.fsBody
                Layout.minimumWidth: 0
                elide: Text.ElideRight
            }
        }

        Rectangle {
            width: 1; Layout.fillHeight: true
            color: Theme.border
        }

        // ── 最近错误(属于同步, 留在这里) ──
        //
        // 进程列表/游标已移到首页。这里只放错误信息 —— 它是同步出问题时
        // 的直接原因, 与状态名同属一块, 放在旁边才说得通。
        Label {
            text: Bridge.lastError
            visible: text !== ""
            color: Theme.err
            font.pixelSize: Theme.fsSmall
            Layout.fillWidth: true
            // 长错误信息必须能省略收尾, 否则 implicitWidth 会把右侧
            // "刷新/打开目录"按钮挤出卡片(实测踩过: 按钮只剩半个字)。
            Layout.minimumWidth: 0
            elide: Text.ElideRight
        }

        // ── 操作区(刷新 + 打开目录) ──
        //
        // 这一列**不能**被压缩: 它是操作入口。空间不够时该被省略/截断
        // 的是左边的长文本, 不是按钮本身(截图里曾出现过"刷新"只剩半个字)。
        // ★ 两个按钮必须**横向**排: 竖排时窄卡片里会叠成两行, 把下面的
        //   "更新 时间"挤出卡片下边缘。
        ColumnLayout {
            spacing: 4
            Layout.alignment: Qt.AlignVCenter
            Layout.minimumWidth: refreshBtn.width + openDriveBtn.width + 6

            RowLayout {
                spacing: 6
                Layout.alignment: Qt.AlignHCenter

                AppButton {
                    id: refreshBtn
                    kind: "ghost"
                    minW: 74
                    text: "刷新"
                    onClicked: Bridge.refreshNow()
                    ToolTip.visible: hovered
                    ToolTip.text: "立即刷新 (每 " + Bridge.refreshSec + " 秒自动)"
                    ToolTip.delay: 500
                }

                // 打开 iCloud 同步根目录。
                //
                // ★ 从顶部固定栏移到这里: 顶栏是所有页面共用的, 而"打开云盘
                //   目录"只对云盘页有意义(书签/密码页顶栏也摆个文件夹图标
                //   同样是噪音)。放在状态卡右侧, 与游标/进程列表同一上下文 ——
                //   这里显示的就是这个目录的同步状态, 点进去看文件最自然。
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

            Label {
                // ★ "更新 时间"已移到首页。
                //   它是**整个程序**的采样时刻(所有页面的数据都来自这一次
                //   采样), 不只云盘页 —— 放在这里会让人误以为只有云盘在刷新。
                Layout.alignment: Qt.AlignHCenter
                text: "云盘数据"
                color: Theme.textLo
                font.pixelSize: Theme.fsTiny
            }
        }
    }
}
