// ======================================================================
//  AppButton.qml  -  全局唯一样式的按钮
//
//  改造前的状况: 12 处 Button 全部自己手写 contentItem + background,
//  结果尺寸出现 72x30 / 56x28 / 48x28 / 60x28 / 76x28 / 72x24 / 30x26 /
//  88x26 / 74x28 共九种, 高度有 30/28/26/24 四档; 边框色的 hover 逻辑
//  也有三套写法(悬停变强调色 / 悬停变 borderHi / 悬停才出现边框)。
//  并排看就是"不齐"。这里收敛成 4 个变体 + 一个统一高度。
//
//  变体:
//    ghost   (默认) 透明底 + borderHi 描边, 悬停底色变浅、描边转强调色
//                    适合设置页里的大多数操作
//    quiet          无边框无底色, 只有悬停时才浮出一层底
//                    适合工具栏上的纯图标按钮
//    primary        实心强调色, 用于页面主操作
//    danger         红描边, 悬停填红 —— 破坏性操作
//
//  高度一律 Theme.btnH; 宽度按内容自适应, 必要时用 minW 兜一个下限
//  (工具栏图标按钮用它固定方形尺寸)。
//
//  文字按钮用 text; 图标按钮用 iconKind(NavIcon 的 kind 名)。
//  两者互斥, iconKind 非空时以图标为准。
// ======================================================================
import QtQuick 2.15
import QtQuick.Controls 2.15

Button {
    id: control

    property string kind: "ghost"
    // 宽度下限。图标按钮没有文字, 靠它撑成方形
    property int minW: 0
    // 文字/图标左右各留的空白, 决定按钮的横向呼吸感
    property int padH: 14
    // 非空则按钮显示 NavIcon 图元而不是文字
    property string iconKind: ""

    // ── 配色 ──
    // 全部走只读属性集中推导, 不在 background/contentItem 里各写一份条件,
    // 否则改一个变体要改三处, 迟早再次走样。
    readonly property color _bg:
          !control.enabled   ? "transparent"
        : control.kind === "primary" ? (control.hovered ? Theme.infoHi : Theme.info)
        : control.kind === "danger"  ? (control.hovered ? Theme.err : "transparent")
        : (control.hovered ? Theme.bgHover : "transparent")

    readonly property color _border:
          !control.enabled   ? Theme.border
        : control.kind === "primary" ? "transparent"
        : control.kind === "danger"  ? Theme.err
        : control.kind === "quiet"   ? (control.hovered ? Theme.borderHi : "transparent")
        : (control.hovered ? Theme.info : Theme.borderHi)

    readonly property color _text:
          !control.enabled   ? Theme.textLo
        : control.kind === "primary" ? "#ffffff"
        : control.kind === "danger"  ? (control.hovered ? "#ffffff" : Theme.err)
        : (control.hovered ? Theme.textHi : Theme.textMid)

    implicitWidth: Math.max(minW, content.implicitWidth)
    implicitHeight: Theme.btnH

    contentItem: Item {
        id: content
        implicitWidth: control.iconKind !== ""
                       ? Theme.navIconSize + control.padH * 2
                       : label.implicitWidth + control.padH * 2
        implicitHeight: Theme.btnH

        NavIcon {
            anchors.centerIn: parent
            visible: control.iconKind !== ""
            kind: control.iconKind
            color: control._text
        }

        Label {
            id: label
            anchors.centerIn: parent
            visible: control.iconKind === ""
            text: control.text
            color: control._text
            font.pixelSize: Theme.fsSmall
        }
    }

    background: Rectangle {
        radius: Theme.radius
        color: control._bg
        border.width: control._border === "transparent" ? 0 : 1
        border.color: control._border
        Behavior on color { ColorAnimation { duration: 120 } }
    }
}
