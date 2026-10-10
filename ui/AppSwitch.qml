// ======================================================================
//  AppSwitch.qml  -  全局唯一样式的开关
//
//  ★ 为什么必须覆盖 indicator 而不是 contentItem
//
//  本项目用 Fusion 风格(QQuickStyle::setStyle)。Fusion 的 Switch 模板是:
//
//      indicator: SwitchIndicator { ... }   ← 药丸 + 圆点画在这里
//      contentItem: Text { text: control.text }  ← 这里只是文字
//
//  所以只覆盖 contentItem 的话, Fusion 自带的那个方角开关**照旧画出来**,
//  我们自制的圆角矩形也画出来 —— 同一位置叠着两个开关图形, 看着"前后不统一"。
//  正确做法是覆盖 indicator, 并把 contentItem 置空(本控件不需要文字)。
//
//  ★ 圆点位移必须用 id 引用控件, 不能写 parent.checked
//
//  圆点是 indicator 的子项, 它的 parent 是 indicator 这个矩形, 不是 Switch。
//  写 parent.checked 取到的是 undefined(矩形没有 checked 属性),
//  三元表达式永远走 false 分支 -> 圆点死死停在左边不动, 只有颜色跟着变。
//  这正是"设置界面的开关开和关位置没变化, 只有颜色变化了"的根因。
//  统一用 control.checked / track.width 这类 id 引用, 不可能再错。
//
//  用法与原生 Switch 完全一致: checked / onToggled / enabled 都照用。
// ======================================================================
import QtQuick 2.15
import QtQuick.Controls 2.15

Switch {
    id: control

    // 尺寸取自 Theme, 三个开关必然一致
    implicitWidth: Theme.switchW
    implicitHeight: Theme.switchH
    // Fusion 模板给的 padding/spacing 是给"图标 + 文字"布局用的, 这里没有文字
    padding: 0
    spacing: 0

    // 置空: 不需要文字, 也就不能让 Fusion 的 Text 参与绘制。
    // 置空后 QQuickControl::contains() 会退化为整个控件矩形都可点,
    // 命中区域是完整的一颗开关, 不会有"只有半边能点"的问题。
    contentItem: null

    indicator: Rectangle {
        id: track

        implicitWidth: Theme.switchW
        implicitHeight: Theme.switchH
        width: Theme.switchW
        height: Theme.switchH
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter

        radius: height / 2
        color: control.checked ? Theme.ok : Theme.bgInput
        border.width: 1
        border.color: control.checked ? Theme.ok : Theme.borderHi
        // 禁用态半透明, 一眼看得出点不动
        opacity: control.enabled ? 1.0 : 0.45

        Behavior on color { ColorAnimation { duration: 160 } }

        Rectangle {
            id: knob
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.switchKnob
            height: Theme.switchKnob
            radius: width / 2
            color: "#ffffff"
            // id 引用, 不存在取错对象的问题
            x: control.checked ? track.width - width - 3 : 3
            Behavior on x {
                NumberAnimation { duration: 160; easing.type: Easing.OutCubic }
            }
        }
    }
}
