# 字幕与弹幕非 UI 内核

本目录负责字幕/弹幕解析、索引、过滤、布局和逐帧绘制指令，不创建控件。SRT、SUP 和弹幕继续
使用各自的现有路径；ASS/SSA 由独立 libass 运行库直接栅格化为透明位图。
后续 D3D11/DirectWrite 呈现层只需消费文字或预乘 Alpha BGRA 位图命令。

## 数据流

1. `SRT字幕解析器` 或 `B站弹幕解析器` 一次加载文件；ASS/SSA 创建独占的 libass 渲染句柄。
2. SRT 文档和 `弹幕资料库` 按开始时间排序，并使用二分时间索引查询。
3. UI 的播放循环读取 `播放器会话.当前快照.播放位置`，创建当前的 `视频显示区域`。
4. 对应帧生成器把当前时间转换成绘制指令；ASS/SSA 按媒体时间生成最小包围位图。

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

SSA/ASS 不再使用简化的托管解析和文字命令，也没有降级路径。`ASS特效字幕帧生成器` 直接调用
`ass_render_frame`，所以脚本图层、`fad/fade`、`pos/move`、`clip`、`t`、卡拉 OK、矢量绘图、
颜色/透明度、字体、描边、阴影、模糊和三维旋转都遵循 libass 语义。原生层按顺序合成局部
`ASS_Image` Alpha 遮罩，直接得到预乘 BGRA；不会分配或扫描完整 4K 视频帧，也不需要黑白双底恢复。
输出保留一像素透明采样边界，再按有效视频区域映射到窗口。

字体按媒体文件所在目录、`Font`、`Fonts` 三处查找，识别 `.ttf`、`.otf` 和 `.ttc`。文件数据通过
`ass_add_font` 载入当前字幕句柄的私有字体库，不安装到 Windows，也不会污染其他播放器实例。
字幕轨道替换、媒体关闭或播放器释放时会依次释放 renderer、track 和 library。

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
常规滚动最大行数、行间距、边距、外描边、四周高斯软阴影、固定弹幕持续时间和相对缩放基准。
呈现器读取画面所在显示器的刷新率作为弹幕目标帧率，跨屏或显示设置变化时自动更新，最高 120 FPS；
阴影深度作为高斯模糊标准差，并按三倍深度向四周预留可见边界；文字精灵首次进入 GPU 图集时完成
模糊，后续滚动帧直接复用。滚动位置直接
使用连续媒体时钟和小数像素；目标帧率只定义呈现节奏及诊断帧序号。弹幕横向以完整客户区为边界，
纵向布局及缩放仍以有效视频高度为基准，因此左右黑边不会暴露突现或突消。窗口尺寸变化会原地调整
仍可容纳的活动项，配置变化会即时更新现有弹幕的样式、速度和布局；收紧类型、行数或数量限制时只
移除不再满足约束的项目。跳转、倒退或替换弹幕源仍会清空活动项并从当前位置继续读取，
不回溯恢复旧弹幕；连续播放只推进时间游标。

```vb
Dim database = B站弹幕解析器.解析文件(path)
Dim filterSettings As New 弹幕过滤配置()
filterSettings.屏蔽用户.Add(userHash)
filterSettings.屏蔽关键词.Add("剧透")

Dim settings As New 弹幕显示配置 With {
    .目标帧率 = 60, .滚动速度 = 180, .同屏最大数量 = 100,
    .常规滚动最大行数 = 5, .顶部最大行数 = 5, .行间距 = 8}
Dim scheduler As New 弹幕调度器(database, settings, filterSettings.创建快照())
Dim commands As New List(Of 弹幕绘制项)(settings.同屏最大数量)
scheduler.生成帧(position, area, commands)
```

`弹幕资料库.查询` 支持关键词、时间范围、用户、类型和结果上限组合查询。屏蔽用户使用不区分大小写
的哈希集合；屏蔽关键词在创建过滤快照时构建多模式状态机，一次扫描文本即可匹配全部关键词。

文档和资料库在构造后只读，可跨线程共享。帧生成器和 `弹幕调度器` 为减少锁与每帧分配而保留
复用状态，应由单一呈现线程使用；设置或过滤条件变化后，在同一线程更新或重建调度器。

播放器当前会自动加载与媒体同名的 `.xml` 文件，并通过现有 Direct2D 到 D3D11 的定时文字图层
在 GPU 上合成。播放中打开或拖入另一份 B 站 XML 时，必须先后台完整解析再原子替换当前资料库；
解析失败不得清空旧弹幕、重建媒体会话或改变播放位置。尚未提供交互选项时，默认使用
`Microsoft YaHei UI`、基础字号 `36`、`60 FPS`、
常规滚动与顶部固定弹幕分别最多 5 行、
同屏最多 `100` 条；真实 HDR 输出会将该图层按纸白亮度转换为 PQ，避免弹幕仍按 SDR 白色输出。

## 测试

独立测试工程会创建受控的内部播放窗口，但只依据命令数、GPU 已绘制序号、纹理可见像素和最终完整
合成帧数等程序数据断言。静态字幕不重复重绘，但必须持续进入由弹幕更新触发的最终交换链帧；测试不使用
画面截图或人工视觉验证。测试还要求 D3D11 逻辑后备缓冲获取次数随最终呈现逐帧增长，防止再次跨
`Present` 缓存 flip-model 缓冲对象；异步 GPU 管线统计必须确认字幕和弹幕两个全屏合成 Pass
都产生实际像素着色器调用，避免错误继承精灵实例管线。可直接使用真实文件：

```powershell
dotnet run --project FFF.Player.Tests -c Debug -- `
  "movie.mp4" "danmaku.xml" ["movie.ass"] ["movie.srt"]
```

测试覆盖 SRT 真实条目数、双语行、libass ASS 特效/字体/预乘 Alpha/释放及 4K 动画基准、B站 mode、搜索和屏蔽、轨道上限、
目标帧率、1080p/2160p 与 DPI 缩放、600 个连续帧、实际 SUP 位图/Alpha/跳转、播放中 XML
原子替换及损坏文件回退，以及通过播放器内核读取实际视频尺寸和时长。

无需媒体文件的定时文字精确诊断会直接检查完整画布生命周期、同一传统量化帧内的小数位移、
Seek 不回溯、DirectWrite 自然对称渲染、灰度抗锯齿、纯外描边、字幕 45 度阴影及弹幕软阴影：

```powershell
dotnet run --project FFF.Player.Tests -c Debug -- --timed-text-regression
```
