# 界面改版（P0 · v2）

本目录是 ProtoSim 的界面设计稿与落地记录。改版目标是把它从"默认 Qt 控件拼起来的样子"
做成一台看着可信的工业仪器：信息密度不降反升，靠层次和留白让人看得下去。

## 文件

| 文件 | 用途 |
| --- | --- |
| `ui-redesign-v1.html` | 静态设计稿：深浅两版并排，含改版说明与设计取舍。**评审用** |
| `ui-prototype-live.html` | 可交互原型：真实 tick、真实 hex、按钮都能点。**感受密度与动效** |
| `icons.html` | 图标集：24px 实尺寸对照 + 应用图标 16/32/64/128 |
| `icons/*.svg` | 图标源文件（改完跑 `python tools/make_icons.py` 重新生成 PNG/ICO） |
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

## 图标集

16 个线性图标 + 一张应用图标，全部由 `tools/make_icons.py` 生成：

- **风格**：24×24 网格、1.6px 描边、圆角端点。**不填色**——颜色由主题令牌给
  （深色 `#8a94a3` / 浅色 `#66738a`），换主题时整组跟着变。
- **产物**：`ui/resources/icons/{dark,light}/`（20px + 40px@2x，进 qrc）、
  `ui/resources/app.ico`（16~256 七档，嵌进 exe）、`ui/resources/app-256.png`（窗口图标）。
- **渲染方式**：Chrome 无头截图，把整组排成无间距网格一次截完再切，比逐个渲染快两个数量级；
  透明背景靠 `--default-background-color=00000000`。
- **踩过的坑**：两档尺寸最初都写同名文件，40px 那轮把 20px 覆盖了，qrc 里只剩一半。

## 表头：填充式拖拽

`ProportionalHeaderView` 实现了"每列可拖 + 总宽恒定 + 每列上下限"：

- 拖宽某列时，其它列按各自"可让出的余量"分摊，**总宽始终等于表格宽度**（不会出现横向滚动条）
- 每列有 min/max（见 `ui/src/mainwindow.cpp` 里的 `setColumnBounds`），谁也挤不没、也不会独占
- 窗口尺寸变化时按当前比例重新铺满

两个反直觉的坑（都写在类注释里）：

1. 不能拿"表头宽度比"当缩放比例——列宽之和本来就不等于表头宽度，比例会被反复放大把列拉爆。
2. 不能拿"当前列宽"反复乘比例——最小宽度会截断，多 resize 几次列越缩越窄。
   正解是记一份权重，每次分配都从权重出发。

这三条不变量由 `tests/unit/tst_headerview.cpp` 断言（ctest 第 10 项）：
铺满后总宽 == 视口、拖宽后总宽不变、拖到极限会被夹在 min/max 内。
