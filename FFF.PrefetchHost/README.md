# FFF.PrefetchHost —— 播放器的「小助手」进程

一句话：它是个**可有可无**的独立 exe。播放器只在运行期空闲的时候才把它拉起来，让它提前帮我们看看「下一个要切到的媒体文件」；它干了什么、甚至它死没死，都不影响正常播放。

## 谁干什么（分工边界）

**小助手只做两件小事：**

- **WARM（顺序预热）**：把邻居文件按 64 KiB 一块、最多前 8 MiB，老老实实顺序读一遍，让操作系统把字节提前放进文件缓存。报告读了多少字节 `warmedBytes`。
- **PROBE（轻量容器预分析）**：用 FFmpeg `avformat_open_input` 只读地打开一下容器，读它自带的流表，把「几个流、视频流是第几个、编码 id、宽高、文件大小、类型标记」这些纯数字（POD）报回来。**不建解码器、不读一帧、不解码**。

**它绝对不碰的东西**：窗口、GPU、音频设备、播放列表、WASAPI、注入泵、`AVFrame`，也**绝不加载 `FFF.Native.dll`**（不引用 `PlayerSession` / `VideoRenderer` / `PlacementPolicy` / `g_logSink`，不链任何 3FP 内核库）。FFmpeg 的 DLL 只由这个小助手自己加载，播放器主进程不替它加载。

**它还会主动「让路」**：进程进 background 模式、设极低内存优先级、放进 Job 里限制 CPU 最多约单核 25%、线程降到最低优先级，并对每个打开的文件标 `IoPriorityHintVeryLow`。这些设置只作用于它自己；缺哪个 API 就静静跳过，绝不报错。日志单独写 exe 同目录的 `prefetch.log`（每行带 pid），**不碰 `kernel.log`**。

## 两边怎么说话（帧格式）

stdin 收任务、stdout 回结果，都是 `[u32 小端长度][payload]`：

- 一帧 = 4 字节「payload 有多长」+ 正好那么长的 payload；payload 就是 `PrefetchProtocol.h` 里定死的 `FFFPrefetchTask` / `FFFPrefetchResult`，全是纯数字，没有指针。
- 任务三种：`WARM=1`、`PROBE=2`、`SHUTDOWN=3`。
- 长度超限或尺寸不对的帧直接丢掉、不回结果；收到 `SHUTDOWN` 或 stdin 断掉（EOF / 管道关闭）就立刻退出，返回 0。
- 回结果时 `generation`、`order`、`pathUtf8` 原样回显（播放器靠 `generation` 丢掉过期的旧结果）；任何一步失败就把 `status` 填成负数，但**照样回一帧、不崩溃**。

## 它崩了会怎样（崩溃语义）

**worker 死 ⇒ 预加载功能直接关闭，播放完全不受影响。**

它只是旁路：预热和预分析本来就是「有更好、没有也行」的加速。拉不起来、中途崩了、管道断了、或者干脆没装这个 exe，播放器主链路照常打开文件、照常解码上屏，最多是切换到下一张图/下一段视频时少了那点预热收益，不会黑屏、不会报错、不会连带拖垮播放器。
