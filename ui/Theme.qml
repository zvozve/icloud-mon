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
        case "空闲":            return idle
        case "同步中":          return info
        case "上传中":          return up
        case "下载中":          return down
        case "双向传输":        return "#a371f7"
        case "暂存中":          return warn
        case "上传处理中":      return warn
        case "未知":            return err
        default:                return idle
        }
    }

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
}
