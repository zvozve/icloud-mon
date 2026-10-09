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
        contentHeight: Math.max(height, col.implicitHeight + Theme.spL * 2)
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
            y: Theme.spL
            spacing: Theme.sp

            // ── 标题行 ──
            RowLayout {
                Layout.fillWidth: true
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
                Layout.preferredHeight: detailCol.implicitHeight + Theme.sp * 2
                color: Theme.bgPanel
                radius: Theme.radius
                border.color: Theme.border
                border.width: 1

                ColumnLayout {
                    id: detailCol
                    anchors.fill: parent
                    anchors.margins: Theme.sp
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
                Layout.preferredHeight: srcCol.implicitHeight + Theme.sp * 2
                color: Theme.bgInput
                radius: Theme.radius
                border.color: Theme.border
                border.width: 1

                ColumnLayout {
                    id: srcCol
                    anchors.fill: parent
                    anchors.margins: Theme.sp
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
