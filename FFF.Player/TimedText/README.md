# 字幕与弹幕非 UI 内核

本目录只负责解析、索引、过滤、布局和逐帧绘制指令，不创建控件，也不依赖 NuGet 或第三方字幕库。
后续 D3D11/DirectWrite 呈现层只需消费 `SRT字幕绘制项`、`ASS字幕绘制项`、`SUP字幕绘制项` 和
`弹幕绘制项`。SUP 使用播放器已有的 FFmpeg 运行时解码，不增加新的外部依赖。

## 数据流

1. `SRT字幕解析器`、`ASS字幕解析器` 或 `B站弹幕解析器` 一次加载文件。
2. 字幕文档和 `弹幕资料库` 按开始时间排序，并使用二分时间索引查询。
3. UI 的播放循环读取 `播放器会话.当前快照.播放位置`，创建当前的 `视频显示区域`。
4. 对应帧生成器把当前时间转换成绘制指令；调用方复用结果 `List`，每帧先 `Clear()`。

`视频显示区域.计算` 的输出尺寸单位是 DIP，DPI 单独传入。它会先换算物理像素，再按视频宽高比去除
上下或左右黑边。字幕字号、弹幕字号、行距、边距和滚动速度都相对于有效视频高度缩放，因此窗口化、
全屏和高 DPI 显示器上的视觉比例一致。

## SRT

SRT 保留多行结构并识别中文、拉丁文字或混合行。`SRT字幕样式` 可配置中英文字体、字号、颜色、
描边、行距和底部边距。样本中形如 `{\fn...}` 的非标准内嵌 ASS 标签会从 SRT 显示文本移除，
避免标签直接显示；SRT 的最终外观始终由用户样式决定。

```vb
Dim document = SRT字幕解析器.解析文件(path)
Dim style As New SRT字幕样式 With {.中文字体 = "Microsoft YaHei UI", .字号 = 42}
Dim generator As New SRT字幕帧生成器(document, style)
Dim area = 视频显示区域.计算(clientWidthDip, clientHeightDip, dpi, videoWidth, videoHeight)
Dim commands As New List(Of SRT字幕绘制项)()
generator.生成帧(position, area, commands)
```

## SSA / ASS

SSA 与 ASS 使用同一份 `ASS字幕文档`、时间索引和绘制指令。解析器同时兼容 SSA `[V4 Styles]`、
`Marked=` 事件和旧式对齐编号，以及 ASS `[V4+ Styles]`、图层和新式对齐编号。

SSA/ASS 使用脚本自己的 `PlayResX/PlayResY`、样式、图层、边距和覆盖标签，不提供用户外观覆盖。
常见标签以及未知标签都会按原文保留；包括 `fad/fade`、`pos/move`、`clip`、`t`、卡拉 OK、颜色、
透明度、字体、字号、描边、阴影、模糊和旋转。`t(...)` 中嵌套的标签保持为一个完整变换。
呈现层应按 `脚本到像素水平缩放`、`脚本到像素垂直缩放` 和偏移映射脚本坐标，并依序应用片段的
`前置覆盖标签`。这样特效严格来自文件定义，不受 SRT 或弹幕设置影响。

`ASS字幕解析器` 会自动识别 SSA 和 ASS；也可以使用名称更明确、但内部完全委托同一实现的
`SSA字幕解析器`。

## SUP / PGS 位图字幕

`SUP字幕解码器` 使用 FFmpeg 的位图字幕解码 API。裸 `.sup` 的 HDMV PGS、容器内 PGS，以及
FFmpeg 能输出 `SUBTITLE_BITMAP` 的 DVD、DVB 和 XSUB 流都走同一原生路径。每次只保留当前解码
事件，多个矩形合成为一个最小包围矩形，并转换为预乘 Alpha 的 BGRA8 像素。

`SUP字幕帧生成器` 根据 PGS 画布坐标映射到 `视频显示区域`，所以黑边、窗口化、全屏和 DPI 变化
不会破坏字幕相对于画面的原始位置。清除事件会立即移除当前位图；文件提供结束时间时也会按结束
时间隐藏。裸 SUP 通常没有随机访问索引，跳转时原生层会重开文件并只解码压缩状态到目标附近，
不会为跳过的字幕创建 BGRA 缓冲区。真实 117 MB 样本跳转到 1 小时位置约需 40 ms。

```vb
Using generator As New SUP字幕帧生成器(path)
    Dim commands As New List(Of SUP字幕绘制项)(1)
    generator.生成帧(position, area, commands)
    ' commands(0).事件.像素BGRA 为预乘 Alpha BGRA8。
End Using
```

## B站 XML 弹幕

mode 1/2/3 映射为常规滚动，4 为底部，5 为顶部，6 为逆向滚动，7 为高级，8 为脚本。
默认只启用常规滚动、顶部和底部；高级和脚本会被解析、索引并允许搜索，但当前基础布局器不执行
它们携带的专用指令。

`弹幕显示配置` 包含字体、字号、源颜色开关、覆盖颜色、滚动速度、目标帧率、同屏最大数量、
常规滚动最大行数、行间距、边距、固定弹幕持续时间和相对缩放基准。目标帧率把播放时间量化为
稳定帧序号，同一目标帧内重复调用不会产生位置漂移。跳转、倒退、窗口尺寸变化或配置变化会从
有限回看窗口重建轨道，连续播放只推进时间游标。

```vb
Dim database = B站弹幕解析器.解析文件(path)
Dim filterSettings As New 弹幕过滤配置()
filterSettings.屏蔽用户.Add(userHash)
filterSettings.屏蔽关键词.Add("剧透")

Dim settings As New 弹幕显示配置 With {
    .目标帧率 = 60, .滚动速度 = 180, .同屏最大数量 = 100,
    .常规滚动最大行数 = 12, .行间距 = 8}
Dim scheduler As New 弹幕调度器(database, settings, filterSettings.创建快照())
Dim commands As New List(Of 弹幕绘制项)(settings.同屏最大数量)
scheduler.生成帧(position, area, commands)
```

`弹幕资料库.查询` 支持关键词、时间范围、用户、类型和结果上限组合查询。屏蔽用户使用不区分大小写
的哈希集合；屏蔽关键词在创建过滤快照时构建多模式状态机，一次扫描文本即可匹配全部关键词。

文档和资料库在构造后只读，可跨线程共享。帧生成器和 `弹幕调度器` 为减少锁与每帧分配而保留
复用状态，应由单一呈现线程使用；设置或过滤条件变化后，在同一线程更新或重建调度器。

## 测试

独立测试工程不需要 UI，可直接使用真实文件：

```powershell
dotnet run --project FFF.Player.TimedText.Tests -c Debug -- `
  "movie.ass" "movie.srt" "danmaku.xml" "movie.mp4" "subtitle.sup"
```

测试覆盖真实条目数、双语行、SSA 旧格式、ASS 覆盖标签、B站 mode、搜索和屏蔽、轨道上限、
目标帧率、1080p/2160p 与 DPI 缩放、600 个连续帧、实际 SUP 位图/Alpha/跳转，以及通过播放器
内核读取实际视频尺寸和时长。
