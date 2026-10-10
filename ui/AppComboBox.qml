// ======================================================================
//  AppComboBox.qml  -  全局唯一样式的下拉选择框
//
//  改造前的状况: 设置页"默认启动页"直接用裸 ComboBox, 走 Fusion 默认皮肤,
//  结果是浅色底 + 灰色边框 + 黑色文字, 夹在一片深色面板里非常突兀,
//  和旁边 AppButton / AppSwitch 完全是两套观感。
//
//  这里把 background / contentItem / indicator / popup / delegate 全部接管,
//  颜色与圆角一律引用 Theme, 高度与 AppButton 对齐(Theme.btnH)。
//  model 用 JS 数组时, 配合 textRole 取每项的显示文本。
// ======================================================================
import QtQuick 2.15
import QtQuick.Controls 2.15

ComboBox {
    id: control

    implicitHeight: Theme.btnH
    padding: 0
    // 右侧留出一块给下拉箭头, 否则文字会跑到箭头底下
    leftPadding: Theme.sp
    rightPadding: Theme.sp + 16

    font.pixelSize: Theme.fsSmall

    // 关闭态显示的当前文本(ComboBox 已按 textRole 算好 displayText)
    contentItem: Text {
        text: control.displayText
        color: control.enabled ? Theme.textMid : Theme.textLo
        font: control.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        leftPadding: control.leftPadding
        rightPadding: control.rightPadding
    }

    // 下拉箭头: ▾ (U+25BE 小实心三角), 随主题色
    indicator: Text {
        x: control.width - width - Theme.sp
        y: (control.height - height) / 2
        text: "▾"
        color: control.enabled ? Theme.textMid : Theme.textLo
        font.pixelSize: Theme.fsBody
    }

    background: Rectangle {
        radius: Theme.radius
        color: (control.pressed || control.hovered) ? Theme.bgHover
                                                    : Theme.bgInput
        border.color: control.hovered ? Theme.info : Theme.borderHi
        border.width: 1
        Behavior on color { ColorAnimation { duration: 120 } }
    }

    // 下拉列表里每一项。
    // ★ 这里刻意**不写 required property**: ComboBox 的 popup 用的是内部的
    //   ComboBoxDelegateModel(model = control.delegateModel), 它不像
    //   ListView/Repeater 那样把 modelData 作为必填角色注入。若声明
    //   required property var modelData, 展开下拉的瞬间才会发现拿不到值
    //   而报 QML 错。Qt 自带 ComboBox 的默认 delegate 用的就是隐式
    //   model[control.textRole] + 隐式 index, 这里照抄那个稳妥写法。
    delegate: ItemDelegate {
        id: row
        width: control.width
        height: Theme.btnH
        highlighted: control.highlightedIndex === index

        contentItem: Text {
            text: model[control.textRole] !== undefined
                  ? model[control.textRole]
                  : String(model)
            color: Theme.textMid
            font.pixelSize: Theme.fsSmall
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            leftPadding: Theme.sp
        }

        background: Rectangle {
            color: row.highlighted ? Theme.bgHover : "transparent"
        }
    }

    popup: Popup {
        y: control.height + 2
        width: control.width
        implicitHeight: contentItem.implicitHeight
        padding: 1

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollBar.vertical: ScrollBar {}
        }

        background: Rectangle {
            color: Theme.bgCard
            radius: Theme.radius
            border.color: Theme.borderHi
            border.width: 1
        }
    }
}