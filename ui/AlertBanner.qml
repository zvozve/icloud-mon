// ======================================================================
//  AlertBanner.qml  -  异常告警横幅
//  仅在 Bridge.anomalous 为 true 时可见 (外层控 visible)
// ======================================================================
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root
    implicitHeight: content.implicitHeight + Theme.sp * 2
    color: "#2d1b1e"                       // 暗红底
    radius: Theme.radius
    border.color: Theme.err
    border.width: 1

    RowLayout {
        id: content
        anchors.fill: parent
        anchors.margins: Theme.sp
        spacing: Theme.sp

        // 图标
        Rectangle {
            Layout.preferredWidth: 22
            Layout.preferredHeight: 22
            radius: 11
            color: Theme.err
            opacity: 0.18

            Text {
                anchors.centerIn: parent
                text: "!"
                color: Theme.err
                font.pixelSize: Theme.fsBody
                font.bold: true
            }
        }

        // 文本
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Label {
                text: Bridge.anomalyTitle
                color: Theme.err
                font.pixelSize: Theme.fsBody
                font.bold: true
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            Label {
                text: Bridge.anomalyDetail
                visible: text !== ""
                color: "#d8a0a0"
                font.pixelSize: Theme.fsSmall
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            // 多个异常类型
            Row {
                spacing: 4
                visible: Bridge.anomalyList.length > 1
                Repeater {
                    model: Bridge.anomalyList
                    delegate: Rectangle {
                        required property string modelData
                        height: 18
                        width: tagText.implicitWidth + 10
                        radius: 3
                        color: Theme.err
                        opacity: 0.2
                        Text {
                            id: tagText
                            anchors.centerIn: parent
                            text: modelData
                            color: Theme.err
                            font.pixelSize: Theme.fsTiny
                        }
                    }
                }
            }
        }

        // 处理按钮
        ColumnLayout {
            spacing: 4
            Layout.alignment: Qt.AlignTop

            Button {
                id: fixBtn
                width: 88; height: 26
                onClicked: Bridge.restartIcloud()
                enabled: !Bridge.busyRestarting
                text: Bridge.busyRestarting ? "处理中" : "重启 iCloud"

                contentItem: Label {
                    text: fixBtn.text
                    color: fixBtn.enabled ? Theme.textHi : Theme.textLo
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.pixelSize: Theme.fsSmall
                    font.bold: true
                }
                background: Rectangle {
                    radius: Theme.radius
                    color: fixBtn.hovered ? Theme.err : "transparent"
                    border.color: Theme.err
                }
            }

            Label {
                Layout.alignment: Qt.AlignHCenter
                text: "重启期间 iCloud 会短暂离线，属正常现象"
                color: Theme.textLo
                font.pixelSize: Theme.fsTiny
            }
        }
    }
}
