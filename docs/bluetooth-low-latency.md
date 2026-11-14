# 蓝牙低延迟模式

## 目标

设置页新增「蓝牙低延迟模式」。默认关闭；打开后，只有当前默认输出端点被识别为**已连接**的蓝牙音频设备时才自动下发音频时间线补偿。设置页仍显示探测到的协议、区间与补偿值，未连接/非蓝牙时补偿为 0。

## 延迟量化

补偿方向来自内核契约：`FFF3FP_SetAudioDelay` 的**正值 = 音频比视频晚**。

```
d = L_disp - L_BT - trim
```

- `L_BT`：耳机侧单程延迟，按蓝牙协议取区间中值。
  - A2DP：150–300 ms，中值 225 ms
  - HFP/HSP：100–250 ms，中值 175 ms
  - LE Audio：30–100 ms，中值 65 ms
  - 识别为蓝牙但服务未知：按 A2DP 区间兜底
- `L_disp`：媒体时钟到期到像素发光的软件侧估计。没有播放会话时拿不到 Present 统计，因此用 `2 × 刷新周期 + 5 ms` 保守估计，并钳在 `[10, 60] ms`。60 Hz 约 38.3 ms，144 Hz 约 18.9 ms。
- `trim`：设置页「额外提前量」，正 = 音频更早，范围 `[-100, 100] ms`，用于吸收设备型号/固件差异。

因此 A2DP 在 60 Hz 屏上约为 `38.3 - 225 = -186.7 ms`，在 144 Hz 屏上约为 `18.9 - 225 = -206.1 ms`。负值表示音频内容提前，不是把音频文件真正预解码到更早的位置。

## 握手/协议从哪里拿

Windows 用户态可稳定拿到的是「默认端点 + 容器 + 蓝牙服务节点」：

1. `IMMDeviceEnumerator::GetDefaultAudioEndpoint(eRender, eConsole)` 取默认渲染端点 ID。
2. 从 `MMDevices\Audio\Render` 端点读取 `ContainerID`（属性 blob 尾部 16 字节 GUID）与 `DeviceState`。
3. 用 `ContainerID` 匹配 `HKLM\SYSTEM\CurrentControlSet\Enum\BTHENUM` / `BTHLE` 服务节点，读取 `Service`、`HardwareID`（服务 UUID）、`FriendlyName` 与实例名中的 MAC。
4. `BthA2dp` / `BthHF` / `BthHsp` / `BthLEAudio` 映射到 A2DP / HFP / HSP / LE Audio。

这只能给出**协议级区间**，拿不到具体 codec（SBC/AAC/aptX/LDAC）和 AVDTP Delay Reporting。前者需要内核 A2DP Sideband IOCTL，后者 Windows 不消费；因此不伪造 codec 级延迟。

## 下发路径

- `播放器控制器.应用蓝牙音频延迟()`：读设置 → 探测默认端点 → 按窗口刷新率算补偿 → 只在值变化时调用 `播放器会话.设置音频延迟(TimeSpan)`。
- 新会话在 `会话_打开完成` 后补放；播放中由 `Form1` 的 500 ms 计时器复查默认端点/协议/连接状态。补偿为 0 且上一次也是 0 时不下发，避免无意义 re-seek。
- 设置页开关或额外提前量改变后立即调用一次；500 ms 轮询仍作为兜底。

## 边界

- 这是**有符号音频时间线偏移**，不是严格 pre-roll。负值让音频内容提前，起播可能跳过与提前量等长的开头音频；连续播放段内音画同步正确。真正的预解码/预填充需要改内核播放管线。
- 端点未连接（`DeviceState != 1`）时一律不补偿；蓝牙耳机回连后最多 500 ms 内生效。
- 蓝牙设备从 A2DP 切到 HFP（或反向）时，协议区间会变化，补偿随之变化；是否正在用 HFP 仍受 Windows 端点统一策略影响，本实现只按当前默认端点父服务节点分类。
- 探测失败只写 `Detail`/调试日志，不打断播放，也不弹窗。
