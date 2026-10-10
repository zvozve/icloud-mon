// ======================================================================
//  Theme.qml  -  全局深色主题 (单例)
//  所有颜色/字号只在这里定义, 组件一律引用, 不写死。
// ======================================================================
pragma Singleton
import QtQuick 2.15

QtObject {
    id: theme

    // ── 背景层次 ──
    readonly property color bgRoot:   "#16181d"   // 最底层
    readonly property color bgPanel:  "#1e2128"   // 面板
    readonly property color bgCard:   "#252932"   // 卡片
    readonly property color bgHover:  "#2e333e"   // 悬停
    readonly property color bgInput:  "#1a1d23"   // 输入框

    // ── 边框 ──
    readonly property color border:    "#333945"
    readonly property color borderHi:  "#454d5c"

    // ── 文字 ──
    readonly property color textHi:    "#e8eaed"
    readonly property color textMid:   "#a8b0bd"
    readonly property color textLo:    "#6b7482"

    // ── 语义色 ──
    readonly property color ok:       "#3fb950"   // 正常/完成
    readonly property color warn:     "#d29922"   // 警告
    readonly property color err:      "#f85149"   // 错误/卡死
    readonly property color info:     "#58a6ff"   // 信息
    readonly property color infoHi:   "#79c0ff"   // 信息高亮(悬停/按下)
    readonly property color up:       "#3fb950"   // 上传
    readonly property color down:     "#58a6ff"   // 下载
    readonly property color idle:     "#6b7482"   // 空闲

    // ── 状态色映射 ──
    function stateColor(name) {
        switch (name) {
        // "已同步"用绿色: 它是**达成状态**(所有队列清空、游标追平),
        // 正是最该让人一眼看到的好结果。灰色太像"停用/无事发生",
        // 会让人以为程序没在工作。(之前担心绿色已被"iCloud 运行中"
        //  占用 —— 实测那个绿是纯文字标签, 状态点是圆点, 不同元素,
        //   不构成语义冲突。)
        case "已同步":          return ok
        case "空闲":            return idle
        // 队列空闲但本地仍有未提交项: 是"还没提交上去", 不是"正在处理"。
        // 用中性提示色, 别用 warn(橙) —— 橙=警告, 会让人以为出问题了。
        case "待提交":          return info
        case "同步中":          return info
        case "上传中":          return up
        case "下载中":          return down
        case "双向传输":        return "#a371f7"
        case "暂存中":          return warn
        case "删除中":          return err
        case "未知":            return err
        default:                return idle
        }
    }

    // ── 导航栏 (仿 UWP NavigationView) ──
    readonly property color navBg:        "#1a1d23"   // 比面板更深一层
    readonly property color navHover:     "#2e333e"
    readonly property color navActive:    "#252932"
    readonly property color accent:      "#58a6ff"   // 选中指示条/强调色
    readonly property int   navW:          200        // 展开宽度
    readonly property int   navWMin:        52        // 折叠宽度(仅图标)
    readonly property int   navItemH:       40
    readonly property int   navIconSize:    16

    // ── 字号 ──
    readonly property int fsTiny:  11
    readonly property int fsSmall: 12
    readonly property int fsBody:  13
    readonly property int fsTitle: 15
    readonly property int fsBig:   20
    readonly property int fsHuge:  30

    // ── 间距 ──
    readonly property int sp:      8
    readonly property int spL:     16
    readonly property int spXL:    24

    readonly property int radius:  6

    // ── 控件尺寸 ──
    // 按钮高度集中在这里。改造前 12 处按钮各自写死, 出现过 30 / 28 / 26 / 24
    // 四种高度, 并排时能看出不齐 —— 统一到这一个值。
    readonly property int btnH:       28
    // 开关轨道尺寸同理, 三个 Switch 原本各写各的
    readonly property int switchW:    42
    readonly property int switchH:    22
    readonly property int switchKnob: 16

    // 滑块尺寸。
    // ★ 这三个值必须给到控件本体: Slider 的 implicitHeight 是从
    //   background/handle 的 **implicit** 尺寸推出来的。原来两个滑块
    //   只给 background/handle 设了 width/height, 没设 implicit*,
    //   于是隐含高度算成 0, 控件被压成一条 —— 界面上轨道整条不见,
    //   只剩一个被裁掉一半的圆点。现在把高度交给控件, 内部按可用高度居中。
    readonly property int sliderH:     20
    readonly property int sliderTrack: 4
    readonly property int sliderKnob:  14
}
