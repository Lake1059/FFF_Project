# 蓝牙低延迟模式

## 目标

设置页新增「蓝牙低延迟模式」。默认关闭；打开后，只有当前默认输出端点被识别为**已连接**的蓝牙音频设备时才自动下发音频时间线补偿。设置页仍显示探测到的协议、区间与补偿值，未连接/非蓝牙时补偿为 0。

## 延迟量化

补偿方向来自内核契约：`FFF3FP_SetAudioDelay` 的**正值 = 音频比视频晚**。

```
d = L_disp - L_BT - trim
```

- `L_BT`：耳机侧单程延迟，按蓝牙协议取区间中值，再叠加端点 PCM 格式开销。
  - A2DP：150–300 ms，中值 225 ms
  - HFP/HSP：100–250 ms，中值 175 ms
  - LE Audio：30–100 ms，中值 65 ms
  - 识别为蓝牙但服务未知：按 A2DP 区间兜底
  - 端点格式未知：格式开销为 0，完全退回协议区间
- `L_disp`：媒体时钟到期到像素发光的软件侧估计。没有播放会话时拿不到 Present 统计，因此用 `2 × 刷新周期 + 5 ms` 保守估计，钳在 `[10, 100] ms`。低刷新率保留完整两帧：24 Hz 约 88.3 ms、30 Hz 约 71.7 ms、60 Hz 约 38.3 ms；高刷新率：144 Hz 约 18.9 ms、240 Hz 约 13.3 ms、360 Hz 约 10.6 ms、500 Hz 钳到 10 ms。
- `trim`：设置页「额外提前量」，正 = 音频更早，范围 `[-100, 100] ms`，用于吸收设备型号/固件差异。

因此 A2DP 在 60 Hz 屏上约为 `38.3 - 225 = -186.7 ms`，在 144 Hz 屏上约为 `18.9 - 225 = -206.1 ms`。负值表示音频内容提前。

## 音频格式项

Windows 用户态拿不到当前 A2DP codec（SBC/AAC/aptX/LDAC/LC3），但能稳定读到默认端点的**设备 PCM 格式**：

- 注册表键：`HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render\<端点>\Properties`
- 值名：`{f19f064d-082c-4e27-bc73-6882a1bb8e4c},0`（`PKEY_AudioEngine_DeviceFormat`）
- 结构：前 8 字节 `PROPVARIANT` 头，之后是 `WAVEFORMATEX`；扩展格式在 `+18` 读有效位深、`+24` 读子格式 GUID。

格式项只估算**主机侧**多声道 downmix、重采样、位深转换的额外排队，不冒充蓝牙 codec 延迟：

| 条件 | 开销 |
|---|---:|
| `声道数 > 2` | +8 ms |
| `采样率 >= 88.2 kHz` | +6 ms |
| `位深或有效位深 >= 24` | +3 ms |
| 合计 | 最多 20 ms |

组合示例（A2DP、60 Hz、trim 0）：

| 端点格式 | `L_BT` | 补偿 |
|---|---:|---:|
| 48 kHz / 立体声 / 16 bit | 225 ms | -186.7 ms |
| 48 kHz / 立体声 / 24 bit | 228 ms | -189.7 ms |
| 96 kHz / 立体声 / 24 bit | 234 ms | -195.7 ms |
| 48 kHz / 5.1 / 16 bit | 233 ms | -194.7 ms |
| HFP 16 kHz / 单声道 / 16 bit | 175 ms | -136.7 ms |
| LE Audio 48 kHz / 立体声 / 24 bit | 68 ms | -29.7 ms |

同一格式在 144 Hz 屏上换成 `L_disp≈18.9 ms`，例如 96 kHz/24 bit A2DP 为 `18.9 - 234 = -215.1 ms`；24 Hz 屏则为 `88.3 - 225 = -136.7 ms`。刷新率与格式两个维度都进入同一个 `L_disp - L_BT - trim`，不是二选一。

## 握手/协议从哪里拿

Windows 用户态可稳定拿到的是「默认端点 + 容器 + 蓝牙服务节点」：

1. `IMMDeviceEnumerator::GetDefaultAudioEndpoint(eRender, eConsole)` 取默认渲染端点 ID。
2. 从 `MMDevices\Audio\Render` 端点读取 `ContainerID`（属性 blob 尾部 16 字节 GUID）、`DeviceState` 与 `PKEY_AudioEngine_DeviceFormat`。
3. 用 `ContainerID` 匹配 `HKLM\SYSTEM\CurrentControlSet\Enum\BTHENUM` / `BTHLE` 服务节点，读取 `Service`、`HardwareID`（服务 UUID）、`FriendlyName` 与实例名中的 MAC。
4. `BthA2dp` / `BthHF` / `BthHsp` / `BthLEAudio` 映射到 A2DP / HFP / HSP / LE Audio。MMDevices 的父设备实例可能带 `{1}.` 前缀（如 `{1}.BTHENUM\...`），探测前会规范化；设备未连接导致服务节点暂时缺失时，会从父路径的服务 UUID 推断协议，仍保留“蓝牙/未连接”状态，不误报为非蓝牙。

这只能给出**协议级区间**，拿不到具体 codec（SBC/AAC/aptX/LDAC）和 AVDTP Delay Reporting。前者需要内核 A2DP Sideband IOCTL，后者 Windows 不消费；因此不伪造 codec 级延迟。端点 PCM 格式单独读取并作为主机侧格式开销，不与 codec 混为一谈。

## 下发路径

- `播放器控制器.应用蓝牙音频延迟()`：读设置 → 探测默认端点 → 按窗口刷新率算补偿 → 只在值变化时调用 `播放器会话.设置音频延迟(TimeSpan)`。
- 新会话在 `会话_打开完成` 后补放；播放中由 `Form1` 的 500 ms 计时器复查默认端点/协议/连接状态。补偿为 0 且上一次也是 0 时不下发，避免无意义 re-seek。
- 设置页开关或额外提前量改变后立即调用一次；500 ms 轮询仍作为兜底。

## 边界

- 执行路径限定为 `FFF3FP_SetAudioDelay` 的**有符号音频时间线偏移**：负值让音频内容提前；不新增预解码/预填充管线。
- 端点未连接（`DeviceState != 1`）时一律不补偿；蓝牙耳机回连后最多 500 ms 内生效。
- 蓝牙设备从 A2DP 切到 HFP（或反向）时，协议区间会变化，补偿随之变化；是否正在用 HFP 仍受 Windows 端点统一策略影响，本实现只按当前默认端点父服务节点分类。
- 端点设备格式变化（例如共享模式从 48 kHz/16 bit 切到 48 kHz/24 bit）会在下一次 500 ms 复查时被读到，格式开销随之更新。
- 探测失败只写 `Detail`/调试日志，不打断播放，也不弹窗。
