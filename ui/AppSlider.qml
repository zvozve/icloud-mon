// ======================================================================
//  AppSlider.qml  -  全局唯一样式的滑块
//
//  ★ 关键: 必须给控件本体一个 implicitHeight
//
//  Slider 的 implicitHeight 是从 background / handle 的 **implicit**
//  尺寸推导出来的:
//      implicitHeight = max(implicitBackgroundHeight + insets,
//                           implicitHandleHeight + paddings)
//  而单纯给 background/handle 设 width/height 并不会设置 implicit* ——
//  普通 Rectangle 的 implicitWidth/implicitHeight 默认是 0。
//  于是 Slider 的隐含高度算成 0 + padding, 控件被压扁:
//  轨道(y = 可用高/2 - 2)和圆点(y = 可用高/2 - 7)一起被推到控件外,
//  界面上表现就是"轨道整条不见, 只剩一个被裁掉一半的小圆点"。
//
//  这里把高度交给控件本身(implicitHeight), 内部元素按 availableHeight
//  居中摆放, 因此在任何尺寸下都不会再被裁。
//
//  变体通过 accent 传色: 刷新间隔用 Theme.info, 卡死阈值用 Theme.warn。
// ======================================================================
import QtQuick 2.15
import QtQuick.Controls 2.15

Slider {
    id: control

    property color accent: Theme.info

    implicitHeight: Theme.sliderH
    // padding 归零, availableHeight 就等于控件高度, 居中计算最直观
    padding: 0

    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.availableWidth
        height: Theme.sliderTrack
        radius: height / 2
        // 未填充段用 borderHi 而不是 bgInput:
        // bgInput(#1a1d23) 与页面底色 bgRoot(#16181d) 几乎同色, 整条轨道
        // 基本看不见 —— 值接近下限时(刷新间隔 5/120)界面上就只剩左边一小块,
        // 完全读不出滑块的总量程。borderHi 是按钮描边同款色, 观感一致。
        color: Theme.borderHi

        // 已选区间
        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: parent.radius
            color: control.enabled ? control.accent : Theme.textLo
        }
    }

    handle: Rectangle {
        x: control.leftPadding + control.visualPosition
           * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: Theme.sliderKnob
        height: Theme.sliderKnob
        radius: width / 2
        color: !control.enabled ? Theme.textLo
             : control.pressed ? Qt.lighter(control.accent, 1.18)
                               : control.accent
        // 描一圈底色, 让圆点和轨道分开, 不然贴合处糊成一块。
        // 用 bgRoot: 设置页直接铺在窗口底色上, 不是面板色。
        border.color: Theme.bgRoot
        border.width: 2
    }
}
