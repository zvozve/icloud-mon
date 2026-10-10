// ======================================================================
//  StatusCard.qml  -  当前状态大卡片
//  显示: 状态名 + 详情 + 同步游标 + 最近错误
// ======================================================================
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root
    implicitHeight: 96
    color: Theme.bgPanel
    radius: Theme.radius
    border.width: 1

    // 窗口缩到最小宽度时, 卡片内容可能比可视区宽。
    // 不裁的话, 右侧的"刷新/更新时间"会画到卡片外面、被页面边界切掉
    // (表现为按钮只剩半个字)。这里显式裁剪兜底 —— 内容超宽时宁可少显示,
    // 也不能让控件溢出容器。
    clip: true

    // 异常时边框转红。原来是"动画中介色 + 无限呼吸", 现在直接静态赋色。
    border.color: Bridge.anomalous ? Theme.err : Theme.border

    // 这里原来是一个 loops: Animation.Infinite 的边框呼吸动画,
    // running 绑定 Bridge.anomalous。
    //
    // 已删除。原因: 卡死是**持续状态**(实测本机停滞已超 800 秒),
    // 也就是 anomalous 长期为 true, 这个无限动画就等于让整个窗口
    // 无休止地按 60fps 重绘 —— 拖动窗口必然卡顿。
    // 异常已经有顶部红色横幅明确表达, 边框静态变红即可,
    // 不靠动画传递信息, 也不丢任何信息量。

    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.spL
        spacing: Theme.spL

        // ── 状态圆点 + 名称 ──
        ColumnLayout {
            spacing: 2
            Layout.alignment: Qt.AlignVCenter

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
                }
            }

            Label {
                text: Bridge.stateDetail
                color: Theme.textMid
                font.pixelSize: Theme.fsBody
            }
        }

        Rectangle {
            width: 1; Layout.fillHeight: true
            color: Theme.border
        }

        // ── 游标 / 错误 / 进程 ──
        ColumnLayout {
            Layout.fillWidth: true
            // ★ 这是"刷新按钮被裁掉"的真正根因, 不是 clip:
            //   RowLayout 会把每个子项的 implicitWidth 累加作为最小需求。
            //   下面三个 Label(游标/错误/进程列表)都是不换行的长文本 ——
            //   进程列表尤其长, 四个进程名逗号连接能有几百像素。它们的
            //   implicitWidth 累加超过卡片宽度后, RowLayout 只能把右侧
            //   "刷新/更新时间"那一列挤出可视区。
            //   Layout.minimumWidth: 0 明确允许这一列被压缩到 0 宽,
            //   再配合下面 Label 的 elide, 长文本就会省略号收尾而不是撑开布局。
            Layout.minimumWidth: 0
            spacing: 4
            Layout.alignment: Qt.AlignVCenter

            RowLayout {
                // 游标行自身也要能收缩, 否则它的 Label 一样会把宽度顶开
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: 6
                Label {
                    text: Bridge.inSync ? "✓" : "⟳"
                    color: Bridge.inSync ? Theme.ok : Theme.warn
                    font.pixelSize: Theme.fsBody
                    font.bold: true
                }
                Label {
                    text: Bridge.rankText
                    color: Theme.textLo
                    font.pixelSize: Theme.fsSmall
                    font.family: "Consolas, monospace"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    elide: Text.ElideRight
                }
            }

            Label {
                text: Bridge.lastError
                visible: text !== ""
                color: Theme.err
                font.pixelSize: Theme.fsSmall
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                elide: Text.ElideRight
            }

            Label {
                text: Bridge.icloudProcesses
                visible: text !== ""
                color: Theme.textLo
                font.pixelSize: Theme.fsTiny
                font.family: "Consolas, monospace"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                elide: Text.ElideRight
            }
        }

        // ── 刷新按钮 ──
        ColumnLayout {
            spacing: 4
            Layout.alignment: Qt.AlignVCenter
            // 这一列**不能**被压缩: 它是操作入口(手动刷新)。
            // 空间不够时该被省略/截断的是左边的长文本, 不是按钮本身。
            // Layout.minimumWidth 取按钮的 implicitWidth, 保证按钮
            // 永远完整可见 —— 这正是截图里"刷新"只剩半个字的原因。
            Layout.minimumWidth: 74

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

            Label {
                // 在 ColumnLayout 里不能用 anchors 居中(会与 Layout 打架),
                // 要用 Layout.alignment
                Layout.alignment: Qt.AlignHCenter
                text: "更新 " + Bridge.timestamp
                color: Theme.textLo
                font.pixelSize: Theme.fsTiny
            }
        }
    }
}
