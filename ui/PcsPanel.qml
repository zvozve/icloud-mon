// ======================================================================
//  PcsPanel.qml  -  云端文件状态分布 (server_items.pcs_state)
//  数据来源在 C++ 侧已聚合成 QVector<PcsBucket>,
//  这里用 Repeater + 比例条展示。
// ======================================================================
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root
    color: Theme.bgPanel
    radius: Theme.radius
    border.color: Theme.border
    border.width: 1

    // 由 C++ Bridge.pcsBuckets 注入: [{pcsState,count,bytes,label}, ...]
    property var buckets: Bridge.pcsBuckets
    property int maxCount: {
        var m = 1
        for (var i = 0; i < buckets.length; ++i)
            if (buckets[i].count > m) m = buckets[i].count
        return m
    }
    property int totalCount: {
        var t = 0
        for (var i = 0; i < buckets.length; ++i) t += buckets[i].count
        return t
    }

    function humanSize(b) {
        var u = ["B", "KB", "MB", "GB", "TB"]
        var v = b, i = 0
        while (v >= 1024 && i < 4) { v /= 1024; ++i }
        return v.toFixed(1) + " " + u[i]
    }

    // pcs_state -> 颜色
    function colorFor(st) {
        switch (st) {
        case 1: return Theme.ok       // 有本地实体
        case 2: return Theme.info     // 仅云端
        case 4: return Theme.warn     // 目录
        default: return Theme.textLo
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.sp
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: "云端状态分布"
                color: Theme.textHi
                font.pixelSize: Theme.fsBody
                font.bold: true
            }
            Item { Layout.fillWidth: true }
            Label {
                text: root.totalCount + " 项"
                color: Theme.textLo
                font.pixelSize: Theme.fsTiny
            }
        }

        Repeater {
            model: root.buckets

            delegate: Rectangle {
                required property var modelData
                required property int index
                Layout.fillWidth: true
                height: 34
                color: "transparent"

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 3

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Rectangle {
                            width: 7; height: 7; radius: 3.5
                            color: root.colorFor(modelData.pcsState)
                        }
                        Label {
                            text: modelData.label
                            color: Theme.textMid
                            font.pixelSize: Theme.fsSmall
                        }
                        Item { Layout.fillWidth: true }
                        Label {
                            text: modelData.count + " 项"
                            color: Theme.textMid
                            font.pixelSize: Theme.fsSmall
                            font.family: "Consolas, monospace"
                        }
                        Label {
                            text: root.humanSize(modelData.bytes)
                            color: Theme.textLo
                            font.pixelSize: Theme.fsTiny
                            Layout.minimumWidth: 62
                            horizontalAlignment: Text.AlignRight
                        }
                    }

                    // 相对比例条 (以最大项为 100%)
                    Rectangle {
                        Layout.fillWidth: true
                        height: 4
                        radius: 2
                        color: Theme.bgInput

                        Rectangle {
                            width: parent.width
                                   * (modelData.count / root.maxCount)
                            height: parent.height
                            radius: parent.radius
                            color: root.colorFor(modelData.pcsState)
                            opacity: 0.8
                            Behavior on width {
                                NumberAnimation { duration: 320 }
                            }
                        }
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }

        Label {
            text: "有本地实体 = 已下载到本机; 仅云端 = 按需下载"
            color: Theme.textLo
            font.pixelSize: Theme.fsTiny
        }
    }
}
