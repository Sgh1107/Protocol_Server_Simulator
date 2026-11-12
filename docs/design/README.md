# 界面改版（P0 · v2）

本目录是 ProtoSim 的界面设计稿与落地记录。改版目标是把它从"默认 Qt 控件拼起来的样子"
做成一台看着可信的工业仪器：信息密度不降反升，靠层次和留白让人看得下去。

## 文件

| 文件 | 用途 |
| --- | --- |
| `ui-redesign-v1.html` | 静态设计稿：深浅两版并排，含改版说明与设计取舍。**评审用** |
| `ui-prototype-live.html` | 可交互原型：真实 tick、真实 hex、按钮都能点。**感受密度与动效** |
| 本文件 | 落地记录：设计令牌怎么映射到 Qt、哪些地方做了取舍 |

两个 HTML 都是自包含的，双击即可在浏览器打开。原型支持
`?theme=light`、`?tab=curve`、`?paused=1` 参数，方便单独看某一视图。

## 落地结构

```
ui/
├── theme.h/.cpp        # 设计令牌（唯一的颜色来源）+ QSS + QML 桥
├── delegates.h/.cpp    # 点表委托（值+量程条+质量胶囊+强制）、报文委托（hex 分色）
├── commandbar.h/.cpp   # 顶部命令栏（品牌 / 运行段控 / 倍速 / 端点胶囊 / 次级按钮）
├── signalrail.h/.cpp   # 左侧设备卡与信号行（自绘 hover/选中）
├── statusstrip.h/.cpp  # 底部状态条（拆成离散读数）
├── mainwindow.h/.cpp   # 装配
└── qml/CurveView.qml   # 曲线：固定量程 Y 轴 + 网格 + 面积 + 当前点
```

## 设计令牌 → Qt

`ThemeTokens` 的字段与原型里的 CSS 变量一一对应（`--bg` / `--panel` / `--line` / `--accent` …）。
深浅主题只换这一组值：QSS 通过占位符替换、委托通过 `setTokens()`、QML 通过 `ThemeBridge`。
**任何单元格里出现的硬编码颜色都是 bug**——主题切换时会露馅。

## 刻意的取舍

| 项 | 决定 | 原因 |
| --- | --- | --- |
| 表头与数值的右边界 | 差约一个单位宽度 | 数值右侧要留单位的位置，QHeaderView 无法按节设置内边距。牺牲 12px 对齐换取单位可读 |
| 解析列 | 只给"读 HR[0..3] · 4 个" | FC 有独立列，重复写"FC03 读保持寄存器"会把 hex 挤没 |
| 曲线纵轴 | 用 `behavior.min/max` 工作带，不用 `signal.range` 量程 | 温度量程是 -40~120，用它会把这根 15~35 的正弦压成直线，"离上限多远"就没了 |
| 报文帧 | 只保留最近 500 行，全量在 TelemetryLog 里导出 | 渲染 5000 行会让 100ms tick 掉帧 |
| 信号名 | 不进解析列 | 列宽有限；按信号过滤走 `signalIds`，导出文本里单独带一段 |

## 落地时修掉的两个坑（写进代码注释了）

1. **`QQuickWidget` 从 qrc 加载是同步的**：`setSource()` 返回时 `Ready` 可能已经发过，
   信号槽后连就永远收不到 → 必须先 `connect` 再 `setSource`，并补一次 `status()` 判断。
2. **`painter->setBrush(cond ? QColor : Qt::NoBrush)` 会走 `QBrush(QColor)`**：
   非强制态的按钮被填成实心黑块。改成显式分支。
