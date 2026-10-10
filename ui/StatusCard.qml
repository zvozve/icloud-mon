// ======================================================================
//  StatusCard.qml  -  同步状态卡(只管同步)
//
//  显示: 状态名 + 详情 + 最近错误 + 操作按钮
//
// ★ 本卡只保留**同步**相关的内容。进程列表、同步游标、卡死告警都已
//   搬到首页 —— 它们描述的是"软件/iCloud 进程运行得怎么样", 属于软件
//   状态, 不是云盘内容。放在云盘页会让人以为只有打开云盘页才能看到
//   程序是否正常。
//
// ★ 布局: 状态名 + 详情一列, 弹性空白, 按钮一列。
//   原来中间还有一根竖分隔线 + 一段 fillWidth 的错误 Label, 结果:
//     - 无错误时 Label 不可见, 那块区域整片空白, 竖线也显得孤立;
//     - 有错误时竖线正好挡在错误文字左边, 像把它框起来, 很怪。
//   去掉分隔线与错误区的独立占位, 错误改为**附在详情下面**(同一列),
//   天然与状态同属一块, 也省掉一根线。
// ======================================================================
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root
    // 一列内容: 状态名行(30) + 详情(17) [+ 错误(16, 仅在有错时)]
    // + 上下边距(spL*2)。让内部 ColumnLayout 自撑, 不用手算。
    implicitHeight: statusCol.implicitHeight + Theme.spL * 2
    color: Theme.bgPanel
    radius: Theme.radius
    border.width: 1
    border.color: Theme.border

    // 窗口缩到最小宽度时卡片内容可能超宽。显式裁剪兜底 —— 内容超宽时
    // 宁可少显示, 也不能让控件溢出容器。
    clip: true

    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.spL
        spacing: Theme.spL

        // ── 左列: 状态名 + 详情 (+ 错误) ──
        ColumnLayout {
            id: statusCol
            spacing: 2
            Layout.alignment: Qt.AlignVCenter
            Layout.fillWidth: true
            // 允许被压缩: 窗口变窄时被截的是长文本, 不是右侧按钮。
            Layout.minimumWidth: 0

            RowLayout {
                spacing: 8
                Layout.minimumWidth: 0

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
                    Layout.minimumWidth: 0
                    elide: Text.ElideRight
                }
            }

            Label {
                text: Bridge.stateDetail
                color: Theme.textMid
                font.pixelSize: Theme.fsBody
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                elide: Text.ElideRight
            }

            // 最近错误: 与状态同列放在详情下面, 不另占一块。
            // 原来它是一个独立的 fillWidth Label, 无错误时不可见却仍把
            // 区域留空, 加上中间的竖线, 整个卡片看着松散。
            Label {
                text: Bridge.lastError
                visible: text !== ""
                color: Theme.err
                font.pixelSize: Theme.fsSmall
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                elide: Text.ElideRight
            }
        }

        // ── 右列: 打开目录 + 刷新 ──
        //
        // 这一列**不能**被压缩: 它是操作入口。空间不够时被省略/截断的
        // 应该是左边长文本, 不是按钮(截图里曾出现"刷新"只剩半个字)。
        //
        // ★ 按钮横向排。竖排时窄卡片里会叠成两行, 把下面的东西挤出卡片。
        //   原来这里还挂着一句"云盘数据"占位文字("更新时间"搬去首页后
        //   随手填的), 毫无信息量, 已删除。
        RowLayout {
            spacing: 6
            Layout.alignment: Qt.AlignVCenter
            Layout.minimumWidth: refreshBtn.width + openDriveBtn.width + 6

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
            //   同样是噪音)。放在状态卡右侧最自然 —— 这里显示的就是这个
            //   目录的同步状态, 点进去看文件顺手。
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
}