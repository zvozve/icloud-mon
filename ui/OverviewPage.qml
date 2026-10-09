// ======================================================================
//  OverviewPage.qml  -  首页（概览）
//
//  回答一个问题: 现在到底是什么状态?
//    - 异常横幅（有异常才出现）
//    - 状态卡: 大状态 + 游标 + 进程
//    - 上传 / 下载 队列
//  「具体哪些文件在等」放到 状态页。
// ======================================================================
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: page

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spL
        spacing: Theme.sp

        // ── 异常横幅 ──
        AlertBanner {
            Layout.fillWidth: true
            visible: Bridge.anomalous
            // 隐藏时要显式把高度收成 0, 否则布局里仍占位
            Layout.preferredHeight: visible ? implicitHeight : 0
            Behavior on Layout.preferredHeight {
                NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
            }
        }

        // ── 状态卡 ──
        StatusCard {
            Layout.fillWidth: true
        }

        // ── 上传 / 下载 队列 ──
        // preferredHeight 给下限, fillHeight 让它在窗口拉高时跟着长
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: 200
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
    }
}
