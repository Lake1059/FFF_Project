# PR #63 正文草稿（GainMap：从"判定对齐"修到"规范判据 + 数值自洽 + 可复现门禁"）

> 本文件是可直接粘贴到 PR #63 描述区的草稿。带 `{{...}}` 的数字由最终验收报告填入，**不在此处编造**。

## 标题建议

```
fix(gainmap): 按 ISO 21496-1 / Apple HDRGainMap 规范重写判据、施加权重与单位，补齐可复现门禁
```

## 正文

### 为什么要改

修复前的语料结果是：外部 41 件 verdict 匹配 41/41、内部 36/38、79 件 × 20 次运行零非零退出。
但这些数字只证明**探针输出与旧期望表自对齐**，并不证明判据符合规范。逐字节复核容器后确认了五处规范缺陷——
其中两处会直接把 base 已是 HDR 的图片错误地"再施加"一遍增益，导致 `maxLinear` 放大到约 437×。
本 PR 的目标是把 GainMap 从"verdict 对齐"修到"规范判据 + 数值自洽 + 可复现门禁"。

同时移除旧 PR 描述里已过期的"本机编译未完成"说明（该环境问题早已不复存在；本次已在本工作树实际编译通过探针与 `FFF.Native`，正文不再保留任何"编译失败/磁盘空间不足"字样）。

---

### 五条核心缺陷与修法

| # | 缺陷 | 证据 | 修法 |
|---|---|---|---|
| 1 | **HDR base 判据错用 `flags` 位**。`flags` 字节只有 `is_multichannel=0x80`、`use_base_colour_space=0x40` 两个有定义位，其余 6 位（`0x3F`）是保留位，**根本没有"HDR base"位**。旧代码却用 `(flags&0x80)&&!(flags&0x40)` 与 bit6-only 判 base HDR。 | 三件 `color_grid_*` AVIF 因此被误判 `applied`，`maxLinear≈437`。 | 删除所有 flags 位判据；HDR base 只由 `hB=baseHdrHeadroomLog2` 与 `hA=altHdrHeadroomLog2` 的大小关系决定。`0x3F` 保留位非零即 `fail_closed_unsupported`。 |
| 2 | **施加无 headroom 权重/方向，`gainMapMax` 被全额当倍率**；JPEG 路径的 `hdrgm:HDRCapacityMin/Max` 被 `(void)` 丢弃。 | `color_grid` 三件 437× 的机制来源；JPEG capacity 元数据完全不参与判定。 | 引入目标 headroom 权重 `w`（见公式），`mult = 2^(w·log2Gain)`，`gainMapMax` 绝不直接当倍率；JPEG `HDRCapacityMin/Max` 必须接入 `hB/hA`。 |
| 3 | **Apple v1 缺 MakerNote 时静默填 `headroom=3.0` 并 applied**，与自身 fail-closed 原则矛盾。 | 与 `meta_makenote00_noxmp_neg` 等负控件口径不一致。 | Apple v1 缺 MakerNote（且 XMP 也无 headroom）→ `fail_closed_unsupported`，不再猜 3.0。 |
| 4 | **`commonDenom = flags & 0x08` 是保留位死分支**，该位置位时整段错读。 | 现 79 件均未置位，属潜伏雷。 | 删除该分支；保留位非零一律拒（与缺陷 1 合并到同一条保留位校验）。 |
| 5 | **量纲跨路径不一致**：tmap 路径用线性 headroom、JPEG/Apple 路径用 log2 stops，同名字段 `headroom` 混用。 | 同名字段无法跨路径对账，数值断言无法写。 | 字段拆分 + 单位标注（见下表），探针 JSON 不再输出裸 `headroom`。 |

> 交付侧配套（P0-3）：`run_corpus.ps1` 与 4 个构建 `.bat` 去除硬编码绝对路径、改为脚本位置推导 + 参数/环境变量覆盖；新增 `docs/gainmap-testing.md` 记录外部 41 件语料的来源/commit/sha256/下载与复跑方法；新增 `workflow_dispatch` 形式的 CI（不挂 push/PR，避免噪音）；本 PR 描述重写。

---

### 单位与字段约定（硬约定，实现与验收共用）

| 语义 | JSON 字段 | 类型 / 单位 |
|---|---|---|
| base 侧 headroom | `meta.baseHdrHeadroomLog2` | double，log2 stops（线性倍率 = `2^`） |
| alternate 侧 headroom | `meta.altHdrHeadroomLog2` | double，log2 stops |
| 线性倍率（仅展示） | `meta.headroomLinear` | double，= `2^max(hB,hA)` |
| 目标 headroom | `meta.targetHeadroomLog2` | double，log2 stops，缺省 4.9 |
| 实际施加权重 | `meta.applyWeight` | double，`[0,1]` |
| 旧裸 `headroom` | **删除** | 不再输出；JPEG 侧改名 `headroomLog2` |

---

### 判据公式

```
hB = baseHdrHeadroomLog2,  hA = altHdrHeadroomLog2        # 单位 log2 stops

若 hB > hA :  verdict = decoded_not_applied               # base 已是 HDR，只显示 base，不施加
若 hB <= hA:  w = clamp((target - hB) / (hA - hB), 0, 1)  # target 缺省 4.9；hA==hB 时 w=1 并记 note

# 施加（ISO 族，gamma>0 已校验）
g        = pow(gainNormalized, 1/gamma)
log2Gain = gainMin[c] + (gainMax[c] - gainMin[c]) * g     # log2 stops，带符号
mult     = pow(2, w * log2Gain)                           # w==0 -> mult=1
out      = (base + w*offSdr[c]) * mult - w*offHdr[c]      # 结果 >=0 钳制；w==0 恒等 base

# Apple 0-base 族
out = base * (1 + (2^(w*headroomLog2) - 1) * gApple)
```

数值上限自证（验收断言）：

```
maxLinear <= 2^(w * max_c(gainMax[c])) * 1.05            # ISO 族
maxLinear <= 2^(w * headroomLog2) * 1.05                 # Apple 族
且 hB > hA 的件不得出现 verdict = applied
```

---

### 新增校验清单（全部 fail-closed）

- [ ] `gainMin[c] <= gainMax[c]`，任一通道违反 → `fail_closed_malformed`（reason 带通道与数值）
- [ ] `gamma[c] > 0`
- [ ] 所有有理数分母 `> 0`（headroom 与每通道各字段）
- [ ] `flags & 0x3F != 0` → `fail_closed_unsupported`（保留位）
- [ ] tmap v0 定长之后仍有剩余字节 → `fail_closed_unsupported`
- [ ] Apple v1 无 MakerNote 且无 XMP headroom → `fail_closed_unsupported`
- [ ] JPEG `HDRCapacityMin/Max` 必须进入 `hB/hA`，不得 `(void)` 丢弃

---

### 测试矩阵（数字由验收报告填入）

| 项 | 结果 |
|---|---|
| 外部 41 件 verdict + 数值双断言 | `{{external_matched}}/41` |
| 内部 38 件 verdict + 数值双断言 | `{{internal_matched}}/38` |
| 新增反例（1ch HDR-base tmap / HDR-base 且 use_base_colour_space=1 / SDR-base 且 use_base_colour_space=1 / 保留位 0xC8 / gainMin>gainMax、gamma=0、分母 0） | `{{neg_pass}}/{{neg_total}}` 通过 |
| 三件 `color_grid_*` 期望翻为 `decoded_not_applied` 且 `maxLinear <= 1.05` | `{{color_grid_ok}}/3` |
| 确定性：≥5 件（含 1 JPEG、1 grid、1 Apple HEIC）各跑 20 次，`appliedRgba` 逐字节哈希一致 | `{{det_pass}}`/20 一致 |
| `FFF.Native.dll` 导出符号数 | `{{export_count}}`（预期 133） |
| 宿主 `dotnet build` 警告/错误 | `{{host_warn}}/{{host_err}}`（预期 0/0） |
| 公共 API 版本 | `{{api_version}}`（预期 20） |

---

### 未闭项（本轮不声称已支持）

- JXL `jhgm` bundle 解析与施加
- 10-bit gain map 路径
- 3×3（非 2×2）grid
- 真实 vendor HEIC（非 Apple、非合成 vendor128）端到端
- GUI 截图级回归（播放器内可见效果）

---

### 复现方式

- 外部语料不进仓库（许可 + 体积）；来源、固定 commit、逐文件 sha256、下载与复跑命令见 `docs/gainmap-testing.md`。
- 本地跑：`tools/run_corpus.ps1 -DryRun` 先看路径解析，再 `tools/run_corpus.ps1` 全量；构建探针用 `FFF.Native/tools/build_probe.bat`（路径全部脚本推导，可用 `FFF_ROOT` 等环境变量覆盖）。
- CI：`.github/workflows/gainmap-probe.yml`，仅 `workflow_dispatch` 手动触发，windows-latest 上 `Release|x64` 构建 `FFF.Native` + 探针 `--help` 冒烟并上传日志。
