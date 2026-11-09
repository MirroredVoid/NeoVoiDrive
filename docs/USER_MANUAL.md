# NeoVoiDrive 用户手册

> 版本 alpha 0.0.1 · Windows 10/11 x64 · VST3 · JUCE 9.0.0
> 本手册面向最终用户，描述当前版本的界面、参数与行为。插件共有 **68 个用户参数**：14 个全局参数（10 个原始 + `engine_version` + T66 Growl 3 个；Engine 仍不在 Settings 显示）+ 每频段 12 个 × 3（含 Band Effects 的整段 Rough）+ 每频段 Sideband 6 个 × 3（Sideband UI 自 T62 起暂时隐藏，参数/预设/自动化保留）。设计依据见 `docs/SPEC.md` §4/§5 与 `docs/DECISIONS.md`。

---

## 1. 简介与适用场景

NeoVoiDrive 是一款面向人声的**三频段质感效果器**。它不改变音高与时长，而是在 1–3 个可自由划分的频段上叠加细微扰动、调制与边带纹理，用于在后期阶段为人声增加"活人感"、粗糙度或复古质感。

核心处理模块：

| 模块 | 作用 | 延迟特性 |
|------|------|---------|
| Jitter | 快速随机音高扰动（无 F0 检测） | 零算法延迟 |
| Shimmer | 快速随机振幅扰动 | 零算法延迟 |
| Vibration | 规则音高调制（Sine / Square / FullWave / Noise，10–300 Hz） | 零算法延迟 |
| Sideband Roughness（UI 暂停） | 围绕各次谐波生成 `k·f0 ± fm` 边带（F0 跟踪调制）；仅由预设/自动化显式选择 Sideband 时启用 | 湿路径基线延迟 L = A + F + 4（见 §5） |
| Growl（T66） | 全局 F0 跟踪幅度调制 + 去刺 EQ（Amount/Rate/Tone）；清音门控，Amount=0 透明 | 零算法延迟；不改变 PDC |
| Formant Shift | 专用 STFT 频谱包络搬移（T68），改变"年龄/体型"音色；0 st 逐位透明 | 开启时 2048 samples（Sideband 引擎再叠加 L） |
| Linkwitz-Riley 分频 | 1–3 段分频，slope 12–72 dB/oct | 零算法延迟 |

适用场景：贴唱与旁白后期、有声书/播客、配音与角色音色设计、Lo-Fi/复古纹理。**不适合**音高纠正、和声生成或复音乐器（Sideband 的 F0 跟踪按单音人声设计）。

---

## 2. 快速上手

1. **加载**：在 DAW 中把 `NeoVoiDrive.vst3` 插到人声轨。当前目标宿主为 Reaper（首要）与 FL Studio（次要）。Standalone 仅用于本地开发/调试，不随发行包分发。
2. **初始状态**：默认 1 个频段、**Legacy 引擎**、全部效果深度为 0，因此**初始内容完全透明**且**不报告额外延迟**（0 samples；Formant 开启时 2048 samples，§5.2）——先播放人声，确认干声正常。
3. **选预设**：点顶栏 **Preset**，从 **17 个**工厂预设中选择（见 §7）。其中 **14 个**使用 Legacy 引擎；`Rough Air` / `Sideband Grit` / `Steel Core` 会显式切换到 Sideband 引擎（该引擎的 UI 目前隐藏，但预设内的 Sideband 参数仍生效）。
4. **做湿声**：在 Band 卡片中使用 Jitter / Shimmer / Vibration 与 **Rough**；需要多段处理时在左栏频段总数单选切到 2 或 3 bands。Sideband 质感目前只能通过工厂预设或 DAW 自动化访问（§3.3）。
5. **想要更亮/更锐（金属芯）**：Sideband Bright 的 UI 自 T62 起隐藏；可直接载入预设 **Steel Core**，或通过 DAW 自动化调节 `b2_sideband_bright` / `b2_sideband_lowcut`（§3.3）。
6. **调界面大小**：Settings 弹窗的 **UI Size** 可选 75 %–150 %，默认 100 %（**1020×612**；1200×720 设计画布的统一缩放）；切换立即生效并按用户保存（§3.5）；Settings 弹窗与下拉菜单同步缩放。
7. **对比**：点顶栏 **Bypass** 切换旁通。旁通使用约 5 ms 等增益斜坡，不会产生 click。
8. **引擎**：Settings 不再显示 Engine 下拉；新实例默认 Legacy。旧工程、自动化或 Sideband 工厂预设若显式保存了 Sideband，仍会按存储值运行（见 §7）。

---

## 3. 界面导览

> **视觉（T64/T65）**：界面采用新拟物风格（同色族表面、柔和双向阴影、大圆角）。强调色按语义区分：**靛蓝 = 选中/焦点/Master 区**、**青 = 频率类**（Xover、Formant）、**橙 = 电平类**（Gain、Dry/Wet、Output Trim）、**洋红 = 效果深度**（Jit/Shim/Vib Depth、Rough）、**紫 = 速率**（Jit/Shim/Vib Rate）；频响区三条频段曲线分别为**珊瑚红（Band 1）/ 琥珀（Band 2）/ 明黄（Band 3）**，与各页签内 Band On 开关、Band Gain 标签/盘面同色。下拉框箭头按语义着色（频率青/速率紫/全局靛蓝），键盘焦点为统一靛蓝环。颜色仅用于识别，不改变任何参数行为。

### 3.1 顶栏

| 控件 | 说明 |
|------|------|
| Logo / 插件名 | 标识区，无交互 |
| **Bypass** | 全局旁通开关；旁通中按钮高亮。约 5 ms 斜坡，无 click |
| **Preset** | 弹出预设菜单（工厂预设按 **Voice Drive / Sound Effects** 两组共 17 项 + User Presets 用户预设库 + 导入/导出/打开文件夹，见 §7） |
| **Settings** | 打开设置弹窗（Quefrency / FFT Frame Size / UI Size / About） |

### 3.2 左栏（约 70%）：分频与频谱区

- **频谱窗口**：显示输入或输出频谱，刷新率 **60 fps**；音频频谱纵轴为 **0 dBFS（顶部）… −72 dBFS（底部）**，三分频指示包络线使用独立的 **+18 dB（顶部）… −18 dB（底部）**映射；视窗内为细线、低透明度网格，横线按 −18…+18 dB 绘制且不标纵轴单位（T70）。显示模式为 **In / Out / Both**（默认 In，Both 时输入曲线降低不透明度）。
- **分频线**：在频谱窗内可拖动 Xover 1 / Xover 2 两条分频线；拖动时显示实时 Hz 读数。
- **频段包络**：三条按频段着色的真实 LR 传递函数包络，随分频点、slope 与 Band Gain 更新。
- **低切预览**：随 Sideband UI 暂停（T62）；渲染代码保留，恢复 Sideband 卡片后可再次使用。
- **左栏下方旋钮行**：Xover 1 / Xover 2 频率旋钮（Hz，整数显示）、Band 1–3 Gain 旋钮（−48…+12 dB）；band 数量单选（1 band / 2 bands / 3 bands）与 Slope 下拉（12/24/36/48/60/72 dB/oct）位于上方同一行。
- **Gain 响度手感（T71）**：三个 Band Gain 旋钮以 **50% = 0 dB** 为默认点；向上提升接近线性（75% ≈ +5.1 dB，100% = +12 dB），向下衰减在中央更慢、越接近 0% 越快（25% ≈ −7.4 dB，1% ≈ −45.3 dB）；**0% 为静音**，读数显示 **−∞ dB**。工程与预设保存实际 dB 值，因此旧工程/预设的响度不变。

### 3.3 右栏：频段卡片

- 效果卡片以**标签页**呈现，标签数量随 `band_count` 在 1–3 之间变化；切到 2 或 3 段后才会出现 Band 2 / Band 3 标签。
- 每张卡片结构（T63）：页签栏（**Band 1/2/3 页签；每个页签内部、Band N 文字左侧是无文字的 Band On 开关**）→ **Band Effects**（第一行 Jit Depth / Jit Rate / **Rough**；第二行 Shim Depth / Shim Rate）→ **Vibration**（Wave 下拉左右与上方 Band Effects 行的 Jit Depth / Rough 旋钮圆面边缘对齐，标签在上方；Depth / Rate 在下方）。所有旋钮按固定 3 列网格纵向对齐；每行最多 3 个旋钮。每个页签开关只控制自己频段；卡片内不再重复 Band N 标题。
- **Band Effects 的 Rough（T50；原 Fog，参数 ID 仍为 `{n}_fog`）**：对**整个 band**（Jitter/Shimmer 与 Legacy/Sideband 引擎混合之后）做恒包络相位雾；Sideband 关闭或 Depth=0 时仍可用。0 = 完全透明；数值越高，谐波中心线越被展成宽峰（T58：Hilbert 正交对 + 20–200 Hz 主相位控制 + 500–3000 Hz 细节控制；**没有音频噪声源、没有 AM、没有延迟线、没有高频 shelf**）。相位旋转与载波频率无关，所以 100–300 Hz 的低次谐波也会随 Rough 变宽。它不改变宿主收到的延迟报告。**听“展宽但谐波仍在”请用 Rough 30–50；Rough 100 是设计上的全雾终点。**
- **Sideband Modulation UI 自 T62 起暂时隐藏**：卡片不再显示 Sideband 小标题、SB On 或 5 个 Sideband 旋钮；`b{n}_sideband_*` 参数、DSP 与 Sideband 工厂预设完整保留，可通过预设、已保存工程或 DAW 自动化访问。恢复 UI 时不需要改参数或 DSP。
- **Band On = off**：该频段全部效果旁路（直通），卡片内控件变灰（页签与其内部的 Band On 开关保持可用）；参数值保留。
- **Harmonic Drive / Harmonic Tone** 两个 Legacy 参数**不再出现在主界面**，但仍可通过 DAW 自动化、工厂预设与已保存的工程调节（仅在 Legacy 引擎下有效）。

### 3.4 右栏底部：Master 区

- Master 卡现在是 **Growl / Master** 两个页签（Growl 在左且默认，Tab 样式与 Band 1/2/3 一致）；顶部两个页签都可点击切换，旋钮/绑定在各自页面内保持。
- **Growl 页**：**Amount**（0 = 关闭）、**Rate**（0.15–0.30× F0）、**Tone**（0 = N3 去刺，负值更亮、正值更暗）。全局 F0 跟踪（100–1000 Hz；低于 100 Hz 的人声暂不触发）；清音/无音高素材自动门控；不做自动电平补偿，实时开启可能降约 1–2 dB RMS，可用 Output Trim 或 **Anger 2** 预设补偿。
- **Master 页**：**Formant Shift**（−12…+12 st，居中）与 **Formant On** 开关（在旋钮右侧）；**Dry / Wet**、**Output Trim**。
- **L/R 电平表**：只显示输出峰值；表条按 Growl 页 Amount/Tone 的圆盘边缘固定，两页共用；**Latency 已移到 Settings**。

### 3.5 Settings 弹窗

| 项 | 说明 |
|----|------|
| **Quefrency** | Formant 倒谱 lifter 截止，0.5–5.0 ms，默认 **0.5 ms**（T68；越小包络越贴合谐波细节，越大越平滑） |
| **FFT Frame Size** | 固定 2048（T68 STFT），显示为禁用态 |
| **Monitor / Latency** | 只读显示当前报告延迟（samples + ms），10 Hz 刷新；Growl 不改变 PDC。Settings 现为 340×496，新增 Monitor 短条 |
| **UI Size** | 界面缩放：75 % / 80 % / 90 % / 100 % / 110 % / 125 % / 150 %，默认 **100 %**。100 % 对应 T63 的 **1200×720 设计画布 → 1020×612**；设置按用户保存（不随工程），切换后主窗口立即缩放；**Settings 弹窗与所有弹出菜单（Preset、Wave、Slope 等）同步缩放**（T72）。Engine 行自 T62 起隐藏（引擎参数仍随工程/预设保存） |
| **About** | 版本与引擎说明文本 |

---

## 4. 参数速览

> `{n}` 为频段前缀 `b1` / `b2` / `b3`；所有连续参数均可自动化。depth = 0 时对应效果在 DSP 中完全旁路，不消耗 CPU。
> DAW 自动化列表中的频段参数显示名带 **`Band N `** 前缀（如 `Band 1 Jitter Depth`），便于区分三个频段。

### 4.1 全局参数（14 个）

| 参数 ID | 显示名 | 范围 | 默认 | 说明 |
|---------|--------|------|------|------|
| `bypass` | Bypass | off/on | off | 全局旁通，顶栏 |
| `band_count` | Bands | 1 / 2 / 3 | 1 | 频段数量，分频区单选钮 |
| `xover1_freq` | Crossover 1 | 40–18000 Hz（对数） | 200 Hz | 第 1 分频点（低/中） |
| `xover2_freq` | Crossover 2 | 40–18000 Hz（对数） | 4000 Hz | 第 2 分频点（中/高） |
| `xover_slope` | Slope | 12 / 24 / 36 / 48 / 60 / 72 dB/oct | 24 | 依次对应 LR2 / LR4 / LR6 / LR8 / LR10 / LR12 |
| `formant_enabled` | Formant On | off/on | off | 全局共振峰开关；开启后报告 2048 samples 延迟（Sideband 引擎再叠加基线 L，见 §5） |
| `formant_shift` | Formant Shift | −12…+12 st（0.1 st 步进） | 0.0 st | 共振峰偏移，改变音色而非音高；0 st 逐位透明（T68） |
| `formant_quefrency` | Quefrency | 0.5–5.0 ms（对数） | **0.5 ms**（T68） | 倒谱 lifter 截止，Settings 弹窗 |
| `dry_wet` | Dry / Wet | 0–100 % | 100 % | 全局干湿混合 |
| `output_trim` | Output Trim | −12…+12 dB（0.1 dB 步进） | 0.0 dB | 输出微调，补偿响度变化 |
| `engine_version` | Engine | Legacy (Harmonic Exciter) / Sideband Roughness | **Legacy (Harmonic Exciter)** | 新旧引擎选择；切换在块边界生效并做 5 ms 交叉淡化。T62 起不在 Settings 显示，但参数保留，旧工程/自动化/Sideband 预设仍可显式选择 Sideband |
| `growl_amount` | Growl Amount | 0–100 % | 0 % | T66 全局 Growl 深度；0 = 逐位透明；Growl 页签 Amount 旋钮 |
| `growl_rate` | Growl Rate | 0.15–0.30 | 0.20 | fm = rate × 跟踪 F0，显示为 0.20× |
| `growl_tone` | Growl Tone | −100…+100 | 0 | 0 = N3 去刺（−3 dB @4 kHz + −1 dB @8 kHz）；负值更亮、正值更暗 |

### 4.2 每频段参数（12 个 × 3 频段，含 Fog）

| 参数 ID | 显示名 | 范围 | 默认 | 说明 |
|---------|--------|------|------|------|
| `{n}_fog`（T49；T50 显示名） | Rough | 0–100 % | 0 | **整段相位雾**（Band Effects 的 Rough，原显示名 Fog）：对引擎混合后的整个 band 输出做恒包络 Hilbert 相位旋转（T58；20–200 Hz 主控制 + 500–3000 Hz 细节控制，R100 4 rad RMS；无音频噪声源/AM/延迟线/底雾），与 Sideband Rough 独立；0 = 透明 |
| `{n}_jitter_depth` | Jitter Depth | 0–1200 cents | 0 | 随机音高扰动峰值 |
| `{n}_jitter_rate` | Jitter Rate | 10–1000 Hz（对数） | 100 Hz | 随机源更新率（噪声带宽） |
| `{n}_shimmer_depth` | Shimmer Depth | 0–100 % | 0 | 随机振幅扰动深度 |
| `{n}_shimmer_rate` | Shimmer Rate | 10–1000 Hz（对数） | 100 Hz | 独立随机源更新率 |
| `{n}_harmonic_drive` | Harmonic Drive | 0–100 % | 0 | **Legacy 参数，不在主 UI**；waveshaper 驱动量 |
| `{n}_harmonic_tone` | Harmonic Tone | 1000–10000 Hz（对数） | 3000 Hz | **Legacy 参数，不在主 UI**；激励高通频率 |
| `{n}_gain` | Band Gain | −48…+12 dB（0% = 静音/−∞） | 0 dB（旋钮 50%） | 每频段响度；T71 非线性手感：50%=0 dB、100%=+12 dB，越远离默认值变化越快；频谱包络随增益更新；工程/预设保存实际 dB。**旧工程的归一化自动化会按新曲线重解释，需重新录制（R57）** |
| `{n}_enable` | Band On | off/on | on | 频段总开关（每个页签内部、Band N 文字左侧的无文字开关）；off 时该段直通、卡片内控件变灰 |
| `{n}_vib_wave` | Vib Wave | Sine / Square / FullWave / Noise | FullWave | FullWave = \|sin(θ)\| |
| `{n}_vib_rate` | Vib Rate | 10–300 Hz（对数） | 10 Hz | 调制率；Noise 波形时为噪声带宽 |
| `{n}_vib_depth` | Vib Depth | 0–200 cents | 0 | 规则音高调制深度 |

### 4.3 每频段 Sideband 参数（6 个 × 3 频段；UI 暂停，参数保留）

> T62 起 Band 卡不显示 Sideband 区，Settings 也不显示 Engine。以下参数仍完整保存在 APVTS/工程/预设中，可通过 DAW 自动化或 3 个 Sideband 工厂预设访问；恢复 UI 不需要改参数。

| 参数 ID | 显示名 | 范围 | 默认 | 说明 |
|---------|--------|------|------|------|
| `{n}_sideband_enable` | SB On | off/on | **on** | 硬开关；off 时该段湿路径强制旁路，参数值保留 |
| `{n}_sideband_depth` | SB Depth | 0–100 % | 0 | 调制深度（同时映射调制指数与湿声增益）；0 = 湿声旁路（内容透明，但仍按基线 L 延迟对齐，见 §5） |
| `{n}_sideband_fm_ratio` | SB Ratio | 0.000–0.500（0.001 步进） | 0.333 | `fm / f0` 比值，默认 1/3（边带间距宽、非整数感） |
| `{n}_sideband_roughness` | SB Rough | 0–100 % | 20 % | 粗粝度：低 = 清晰细谱线，高 = 围绕谐波的宽峰（T58；与 Band Effects Rough 共用 FogCore：Hilbert 相位旋转、20–200/500–3000 Hz 控制、R100 4 rad RMS；无音频噪声源/AM/额外延迟）；**雾宽只随 Rough 变化，不随 Depth**；只作用于湿路，dry 满电平；Low Cut 会限制能被雾化的最低频段 |
| `{n}_sideband_lowcut` | SB Low Cut | 100–8000 Hz（对数） | 2000 Hz | 湿路径高通截止（24 dB/oct）；以下保持纯干声 |
| `{n}_sideband_bright` | SB Bright | 0–100 % | **0** | **与 Rough 正交的亮度/临场轴（T40）**：频段输出 1.5 kHz 高频倾斜 + 3.2 kHz 临场钟形；只变亮/变锐，不改变雾化宽度；0 = 完全透明且零额外 CPU |

> 以上旋钮在 T62 起的 Band 卡中不再显示；参数 ID 仍带 `{n}_sideband_` 前缀，预设/自动化/旧工程继续有效。

### 4.4 参数边界规则

- `xover2_freq` 的实际生效值被钳制为 `max(xover2, xover1 × 1.05)`；拖动时 UI 亦保证 xover1 < xover2。
- `band_count` 减少时，被隐藏频段的参数值保留在工程状态中，恢复段数后原值还原。
- 播放中切换段数：输出在约 5 ms 内淡出，在静音点切换路由并清空被隐藏/新启用频段的延迟历史，再约 5 ms 淡入（避免 click 与旧延迟内容）。
- `{n}_sideband_enable` = off 等价于该段 depth 强制为 0；T62 起 Sideband 旋钮不在 UI 显示，但参数值保留。
- 雾化（Rough）的延迟摆幅上限固定为 `F = round(34·sr/48000)` 样本（48 kHz = 34），**与 Depth 无关**；SB Depth 只改变边带强度与湿声增益（T43/T44）。
- 有效 `fm = fm_ratio × f0` 被钳制在 1 Hz–8 kHz 之间，避免极端采样率组合发散。
- `engine_version` 切换只在块边界生效并做 5 ms 交叉淡化。

---

## 5. 延迟与 PDC 说明

### 5.1 报告延迟

插件按以下规则向宿主报告延迟（`setLatencySamples`），宿主据此做 PDC 自动补偿：

| 状态 | 报告延迟 |
|------|---------|
| Engine = Legacy，Formant 关 | **0 samples** |
| Engine = Legacy，Formant 开 | **2048 samples** |
| Engine = Sideband（任意 depth / band_count / SB On / Band On），Formant 关 | **Lmax samples** |
| Engine = Sideband（任意 depth / band_count / SB On / Band On），Formant 开 | **2048 + Lmax samples** |

> **Growl（T66）不新增延迟线**：无论 Amount/Rate/Tone 或是否与 Sideband/Formant 叠加，报告延迟表都不变；F0 仅进入控制率。
>
> **默认实例（T62）使用 Legacy，报告 0 samples（Formant 关）/ 2048 samples（Formant 开）。** 只有显式选择 Sideband（旧 v2 工程、DAW 自动化或 Sideband 工厂预设）才进入 Lmax 行：Sideband 引擎使用**固定最大基线 Lmax**，报告值与 depth、Band On、SB On、band_count 都无关；48 kHz 为 **53 samples（≈1.10 ms）**。depth 自动化不改变报告值，避免宿主反复重补偿。所有未处理路径（全局 Dry/Wet 的 dry、Bypass、Band On 关闭的 dry）都延迟到同一报告值。

Formant 路径的 2048 samples 换算：

| 采样率 | 44.1 kHz | 48 kHz | 88.2 kHz | 96 kHz | 176.4 kHz | 192 kHz |
|--------|----------|--------|----------|--------|-----------|---------|
| 延迟 | 46.44 ms | 42.67 ms | 23.22 ms | 21.33 ms | 11.61 ms | 10.67 ms |

**播放中切换 Formant**：开启的那一块起，报告值立即变为 2048(+Lmax)，插件输出同时切到 2048 延迟的 dry——因此 2048 样本的引擎预热期（48 kHz 约 42.7 ms）与宿主补偿始终对齐；预热结束后在 5 ms 内淡入 formant 湿声。关闭时报告值立即回落到 0(Lmax)，湿路在 5 ms 内淡回直通（这 5 ms 是唯一允许的亚块级过渡偏差）。加载工程/预设时（包括宿主停止状态）报告值也会立即刷新。

### 5.2 Sideband 湿路径基线 Lmax

- 定义：**固定** `Lmax = A(100%) + F + 4`，其中 `A(100%) = 2/(2π·1000)·sr`（depth=100% 的最坏摆幅），`F = round(34·sr/48000)`（48 kHz 时 34 样本 ≈ 0.708 ms）是历史雾延迟预算，T58 起**仅用于保持 PDC/报告值不变**，不再有 fog 读取。Depth 只缩放固定窗口内的调制幅度，**不改变 Lmax**。
- **Lmax 的数值**（SB-L01 实测）：

| 采样率 | Lmax |
|--------|------|
| 44.1 kHz | 49 样本（1.11 ms） |
| 48 kHz | **53 样本（1.10 ms）** |
| 96 kHz | 103 样本（1.07 ms） |
| 192 kHz | 201 样本（1.05 ms） |

- 全局 Dry/Wet 的 dry、Bypass、Band On 关闭的 dry 以及 band 内部 dry 都使用同一 Lmax（Formant 开启时再加 2048），因此干/湿、旁通与多频段求和无时偏；报告 Lmax 使宿主 PDC 覆盖整条输出。
- 报告条件只取决于 **Engine = Sideband（显式选择）**；Band On/SB On/depth/band_count 开关都不会改变报告值。默认 Legacy 实例不承担 Lmax，报告 0/2048。

### 5.3 需要明确的折衷

- **Formant 开关**：Sideband 引擎下报告值在 **Lmax 与 2048 + Lmax** 之间切换，插件对输出做约 5 ms 交叉淡化；宿主可能需要一次 PDC 重补偿。重新开启 Formant 后需要重新积累分析帧，短瞬间的静音属正常现象，不建议在现场演出中频繁切换。
- **Oversampling**：Legacy 引擎的 2x 过采样使用 polyphase IIR half-band，实际群延迟小于 10 samples（非整数），按 0 报告（DECISIONS R11）；这是"非 formant 路径接近零延迟"目标下的明确折衷。
- **Bypass 不改变报告延迟，且不再产生时间跳变**：Bypass 只对音频做约 5 ms 斜坡，报告值保持不变；Bypass 的干声同样延迟到当前报告值（Legacy 0/2048；Sideband Lmax/2048+Lmax），因此 A/B 不会出现 38–2048 样本的时间偏移（T45；R16 的旧权衡已被对齐实现取代）。
- **F0 跟踪滞后**：约 31 ms，只作用于控制信号（边带频率 fm），不在音频路径上，不会增加报告延迟。

---

## 6. 音质与听感提示

- **零效果时透明**：在 3 段、slope 24 dB/oct、所有效果参数为 0 的配置下，20 Hz–20 kHz 幅度响应平坦在 ±0.5 dB 内（A02 验收标准，交叉区外实测最差 0.25 dB）；分频点邻域的 LR4 求和凹陷为设计行为（A02/O3 留档）。
- **分频点染色**：Linkwitz-Riley IIR 分频在分频点邻域存在固有幅度/相位叠加染色。对"干净人声"场景，建议使用 **LR4（24 dB/oct，默认 slope）**。
- **高调制率 + 大深度会产生 FM 边带**：Vib Rate 高于约 100 Hz 且深度较大时，音色会金属化——这是预期行为（R4），不是失真或故障。
- **FullWave 波形**：`FullWave = |sin(θ)|`，是连续拱形波，等效调制频率为设定 Rate 的两倍，听感比 Sine 更"颤"但不跳变。
- **Shimmer 只衰减不放大**：增益范围约为 `[1−depth, 1]`，depth = 100 % 时最暗，不会因调制产生 +6 dB 削波。
- **Sideband 的固有倾向**：调制指数随频率增长（β ∝ f），高次谐波的边带更强、载波损失更明显；可用 Band Gain 补偿。roughness 低时是清晰细谱线，高时是**细颗粒雾状纹理**（T43「fine fog」：颗粒尺寸约 0.17 ms）。
- **Rough 的细节（T58/T59/T60）**：`FogCore` 用 8 段全通 IIR Hilbert 对把输入变成解析信号，再用随机相位控制做恒包络旋转 `i·cos(phi) − q·sin(phi)`。相位控制分 20–200 Hz 主带（0.70）与 500–3000 Hz 细节带（0.30），R100 为 4 rad RMS；它只驱动相位，**不进入音频**，所以没有附加噪声源/底雾，也没有 AM/音量抖动。T59 的细节优先（0.60/0.40）虽然把颗粒切细，但会把较远的侧带推多，听成“谐波下面垫一层雾”；T60 收回为 0.70/0.30：相位率约 588 Hz（T58 约 459 Hz）、远侧带约 0.9 %（T59 约 1.8 %），在更细的颗粒和不铺底之间折中。R50 中心线约 32 %（R100 才接近全雾），能量成为围绕谐波的宽峰。无延迟线；Rough=0 逐位透明；Band Effects 与 Sideband 共用同一核。**听展宽用 Rough 30–50，R100 是全雾终点**。实测 Band Effects：1 kHz R30/R50 中心线 ±5 Hz 86.11/32.02 %、±100 Hz 内 95.40/69.84 %、总 RMS +0.01/+0.02 dB、10 ms 包络 CV 0.022/0.029；**100 Hz 中心线 88.7/40.0 %、±100 Hz 内 97.5/81.0 %**；Sideband wet R30/R50 线幅 0.201/0.114、5–20 Hz 包络 0/0 %。
- **Rough 的峰值与响度**：相位旋转保持总 RMS，但会改变瞬时峰值（试听素材峰值约 0.35–0.38；接近 0 dBFS 的素材建议留 3–6 dB headroom）。Rough 100 只是全雾端点，不建议用它做音色验收。
- **Rough 与 Bright 是两条正交的轴（T40/T43）**：Rough 只控制边带/雾化的宽度，Bright 只控制频段输出的高频/临场亮度，两者互不改变对方的定义（T43 后 3.2 kHz 提升在 Rough 0/100 间实测差 2.14 dB，仍在 3 dB 正交容差内）。三种典型组合：
  - 想要"细线 + 锐利"：Rough 0–20、Bright 60–100（干净、金属芯、齿音清楚）；
  - 想要"细雾"：Rough 40–100、Bright 0（T43 后的细颗粒雾化质感）；
  - 想要"明亮的雾"：Rough 60–100、Bright 40–70（既有颗粒又有光泽）。
- **Bright 的"金属芯"**：3.2 kHz 钟形抬的是 presence 区，配合 Sideband 的非谐波边带，听感更锐、更"金属"；它只提升已有内容，所以放在**中频段（Band 2）**最有效（默认分频点 200/4000 时 Band 2 正好覆盖 2–4 kHz 芯区），Band 1 通常几乎无感。Bright 推到 100 % 时 3–8 kHz 最多约 +10 dB，若齿音过亮用 Band Gain 或 Output Trim 回收。
- **Sideband 的低切**：SB Low Cut 以下保持纯干声，基频附近不被调制；若某频段的频率内容整体低于该段 lowcut，该段 Sideband 会自然变安静，这不是缺陷。
- **Formant Shift**：只搬移频谱包络，不改变基频与时长；T68 起使用专用 STFT 包络搬移（无 Vocoder/变调），**shift = 0 时与输入逐位透明**（核心 SNR ≈138 dB；全链 A06 46.5 dB）。搬移仅在 150–5500 Hz 内生效，低频根基与 5.5 kHz 以上空气感保留。
- **Dry/Wet 与 Formant**：Formant 开启时干路径会被对齐到 2048 samples（Sideband 引擎为 2048 + L），`Dry/Wet < 100 %` 不会产生 42.7 ms 回声。

---

## 7. 预设与状态保存

工厂预设共 **17 个**（14 Legacy + 3 Sideband），全部显式携带 `engine_version`，加载结果与加载顺序无关。Preset 菜单按 **Voice Drive**（Growl 1/2/3、Anger 1/2/3）与 **Sound Effects**（其余 11 个）两个分组显示；下表编号是预设库内部顺序，不代表菜单顺序：

| # | 预设 | 引擎 | 特点 |
|---|------|------|------|
| 1 | Clean Voice | Legacy | 干净人声起点 |
| 2 | Warm Voice | Legacy | Formant 轻微下移 + 低段激励 |
| 3 | Radio Voice | Legacy | 中频段激励与抖动 |
| 4 | Lo-Fi Tape | Legacy | 高频段抖动/闪烁与轻微颤动 |
| 5 | Pitch Up | Legacy | Formant 上移（音色变年轻） |
| 6 | Pitch Down | Legacy | Formant 下移（音色变厚重） |
| 7 | Bright Exciter | Legacy | 高段强激励 |
| 8 | Vintage Vib | Legacy | 低频段 Sine 颤动 + 抖动 |
| 9 | Rough Air | **Sideband** | 中度 depth、细边带质感（lowcut 300 Hz） |
| 10 | Sideband Grit | **Sideband** | 高 depth + 100 % roughness，细密重雾（T43/T44 起为细颗粒、T44 明显加强；lowcut 100 Hz） |
| 11 | Steel Core | **Sideband** | 自动切 3 段；低 Rough + 高 Bright（b2 80 / b3 85），锐利、明亮、金属芯 |
| 12 | Growl 1 | Legacy | Growl 2 的**轻档**：中频 128 Hz 边带 + b2 Jitter 220，b3 Rough 18 / +4 dB；粗糙度收敛、边带仍可辨 |
| 13 | Growl 2 | Legacy | **原 T61 Growl 1**：自动切 3 段；b2 Sine 128/120 + Jitter 320/375，b3 Rough 30 / +6 dB；粗糙但边带仍可辨认的嘶吼质感 |
| 14 | Growl 3 | Legacy | Growl 2 的**重档**：b2 Jitter 500，b3 Rough 45 / +7 dB；更密、更粗糙 |
| 15 | Anger 1 | Legacy | N3 **轻档**：基座 Jitter 60 / Rough 6 + 全局 Growl Amount 60 / Rate 0.20 / Tone 0；Output Trim +1 dB |
| 16 | Anger 2 | Legacy | **原 T66 Growl 2（N3 验收版）**：3 段 Legacy 基座（1k/4k）+ 全局 Growl Amount 100 / Rate 0.20 / Tone 0；Output Trim +2 dB |
| 17 | Anger 3 | Legacy | N3 **重档**：基座 Vib/Jitter 180 / Rough 22 / +4 dB + Growl Amount 100 / Rate 0.15（更慢）/ Tone +25（更暗）；Output Trim +2 dB |

- **两条三档家族**：`Growl 1/2/3` 来自 Legacy 频段调制（不使用全局 Growl）；`Anger 1/2/3` 在 Legacy 基座上叠加全局 N3 Growl。编号越大越重，`* 2` 分别是原 T61 / T66 预设。
- **工厂预设按完整快照加载**：先恢复全部参数的默认值，再应用预设显式定义的参数。因此不会残留上一个预设的 Bypass、效果深度或隐藏频段设置；`Clean Voice` 即真正的中性起点。
- **频段数量随预设确定**：`Radio Voice`、`Lo-Fi Tape`、`Bright Exciter`、`Rough Air`、`Sideband Grit`、`Steel Core`、`Growl 1/2/3`、`Anger 1/2/3` 会自动切到 3 段（`band_count`），确保它们使用的 b2/b3 参数真正生效。
- **状态版本**：工程状态写为 version 2，包含全部参数。
- **v1 旧工程**（无版本号或 version 1，仅含 37 个旧参数）：载入后旧参数原位恢复、新参数取默认，并**强制 `engine_version = Legacy`**（DECISIONS R20），保证旧工程声音逐位不变。
- **隐藏频段**：减少 band_count 不会丢失被隐藏频段的参数值（R12）。
- **引擎随工程保存**：`engine_version` 是普通参数，随 DAW 工程、预设与自动化完整保存；T62 起 Settings 只是不再显示下拉。
- **Sideband 预设**：`Rough Air` / `Sideband Grit` / `Steel Core` 仍显式携带 `engine_version = Sideband`，加载后会按预设内的 Sideband 参数发声；只是 Band 卡不再提供对应旋钮。
- **⚠ T71 Band Gain 与旧自动化（DECISIONS R57）**：工程/预设保存的是**实际 dB**，所以旧工程的声音不变；但 DAW 里保存的**归一化自动化曲线**会按新的响度曲线重新解释（例如归一化 0.5 过去约为 −18 dB，现在是 0 dB）。旧工程若录有 Band Gain 自动化，请重新录制，或把自动化改为 dB 值后重新写入；这不是可以自动迁移的兼容项。

### 7.1 自定义预设（导入 / 导出，T42）

Preset 菜单结构：**Voice Drive**（Growl 1/2/3、Anger 1/2/3）→ **Sound Effects**（其余 11 个工厂预设）→ **User Presets ▸**（用户预设库）→ **Export Preset… / Import Preset… / Show Preset Folder**。

- **导出（Export Preset…）**：把当前全部参数保存为 `.nvpreset` 文件；对话框默认打开用户预设目录，**文件名即预设名**（扩展名自动规范为 `.nvpreset`）。导出时会把 `Bypass` 在文件里强制关闭，因此预设不会以旁通状态被导入；当前正在编辑的状态不受影响。T39 的 `Debug: Sideband solo` 已于 T46 移除。
- **导入（Import Preset…）**：选择 `.nvpreset` 文件（也接受从 DAW 导出的状态 `.xml`）后立即恢复全部参数；若预设改变了 `band_count`，频段标签页会自动跟随。
- **用户预设库**：`%APPDATA%\NeoVoiDrive\Presets\`（**Show Preset Folder** 直接打开）。该目录下的 `.nvpreset` 会按文件名排序显示在 **User Presets** 子菜单中，点击即加载；重命名 / 删除 / 备份直接用文件管理器。导出到其他目录的文件不会自动出现在菜单里，需要用 Import 导入，或再次 Export 到默认目录。
- **文件格式**：UTF-8 纯文本 XML（可读、可 diff、可分享）：外层 `<NeoVoiDrivePreset formatVersion="1" stateVersion="2" name="...">`，内层是完整的参数状态。保存自更新版本的预设会提示"格式过新"；损坏、非预设或超过 1 MB 的文件会被拒绝，且**不会改变当前参数**。
- **旧工程兼容**：v1 状态（`version="1"`）导入后强制 `engine_version = Legacy`（与旧工程一致，R20）；预设中缺失的参数保持当前值（与 DAW 状态恢复一致）。
- **注意**：UI 缩放（Settings → UI Size）是用户级设置，不属于预设，不会随 `.nvpreset` 导入/导出。

---

## 8. 常见问题 / 已知限制

| 问题 | 说明 |
|------|------|
| 打开插件后听不出效果 | 默认状态下所有深度为 0，插件是透明的；先载入预设或拉高 Jitter / Shimmer / Vib Depth / Rough |
| 默认显示多少延迟 | T62 起默认 Legacy：Formant 关 **0 samples**；Formant 开 **2048 samples**。只有 Sideband 预设/旧工程才报告 Lmax。 |
| Sideband 控件在哪里 | T62 起暂时隐藏（DSP/参数/预设保留）。可载入 3 个 Sideband 工厂预设，或通过 DAW 自动化编辑 `b{n}_sideband_*`。 |
| 打开 Formant 后延迟变大、工程重新对齐 | Formant 开启会报告 2048 samples（显式 Sideband 引擎为 2048 + Lmax）并由宿主 PDC 补偿，属预期（§5） |
| Bypass 时仍显示 2048 samples（Sideband 引擎为 2048 + Lmax） | 报告值不随 Bypass 改变（R16），避免宿主 PDC 抖动 |
| Sideband 深度为 0，为什么仍报告 Lmax | 显式选择 Sideband 后，该引擎使用固定湿路径基线（含 depth 0）：band 输出是按 Lmax 延迟匹配的 dry，必须上报让宿主补偿（§5.1） |
| 如何切换 Engine | T62 起 Settings 不再显示 Engine；新实例默认 Legacy。旧工程/自动化/Sideband 预设中的 `engine_version` 仍生效。 |
| 低频段拉 Sideband 却没声音 | Sideband 参数目前只能经预设/自动化访问；SB Low Cut 高于该段内容时湿路径近似无输入 |
| 拉了 Bright 但某些频段没变化 | Bright 是 Sideband 参数，T62 起 UI 隐藏；可在 Sideband 预设/自动化中调节。Bright 提升频段输出 1.5 kHz 以上内容，Band 1 通常无感 |
| Bright 会不会改变 Rough 的雾化 | 不会。Bright 是独立的线性高频/临场提升，不触碰噪声源与边带间距；Rough 仍单独控制雾化（T40/T43；T43 后 3.2 kHz 提升在 Rough 0/100 间实测差 2.14 dB，仍在 3 dB 正交容差内） |
| Rough 拉高会不会发刺/齿音重 | T58 无 HF shelf；相位雾的输出是恒包络旋转，R50 wet-solo 6–16 kHz 占比 0.09 %，不会加高频嘶声；若仍偏亮，优先降 Bright 或 Band Gain，而不是降 Rough |
| Rough 拉满后低频还听得出音高，正常吗 | 正常。相位旋转不改变中心频率，左右对称的相位控制保留音高锚；能量会从中心线移到宽峰。若要连基频一起打散，需要更大相位偏差/不同控制带，属于后续结构性选项。 |
| 为什么低音现在也会变宽了 | T58 的相位控制与载波频率无关，100–300 Hz 的谐波与高频获得同样的 Hz 级展宽；R50 中心线约 32–40 %（R100 才接近全雾），能量成为宽峰。Sideband 作用域还要受 Low Cut 限制。 |
| Bright 推到很高时齿音/电平发冲 | Bright 是高频提升（100 % 时 3–8 kHz 最多约 +10 dB），必要时降低 Band Gain 或 Output Trim |
| 分频点附近有轻微染色/起伏 | LR IIR 分频的固有特性（干净人声建议 LR4）；不同频段设不同 Sideband depth 时，交叉区还会因干/湿基线延迟差产生干涉，实测最差约 3.5 dB（T33 留档），非缺陷 |
| DAW 自动化列表里的 Harmonic Drive/Tone | Legacy 参数，主 UI 已隐藏，仅 Legacy 引擎下生效 |
| Sideband 在复音/乐器素材上不稳定 | F0 跟踪针对单音人声（100–1000 Hz）；复音时降级但不失控，气声段会自动淡出湿声 |
| 支持哪些格式/平台/采样率 | Windows x64 的 VST3（发行包；Standalone 仅本地开发调试，不随包分发）；44.1 / 48 / 88.2 / 96 / 176.4 / 192 kHz（异常值回退 44.1 kHz）；不支持 AU/AAX/CLAP、macOS/Linux、环绕声 |
| DAW 手动验收 | SB-D01–D06 手动矩阵仍待用户实际执行（开发记录 T35 §4，未随仓库分发）；本手册的界面/参数描述为当前权威来源 |

---

## 9. 文档索引

| 文档 | 用途 |
|------|------|
| `docs/USER_MANUAL.md` | 本手册：界面、参数速览、延迟与 PDC、预设与限制 |
| `docs/SPEC.md` | 产品需求规格（§4 参数、§5 延迟/缓冲预算） |
| `docs/ARCHITECTURE.md` | 架构总览、目录结构与模块职责 |
| `docs/AGENTS.md` | 协作约定与架构摘要 |
| `docs/THIRD_PARTY_LICENSES.md` | 第三方许可与发行包内容 |
| `docs/README.md` | 开发/构建/测试说明 |

> 设计决策、测试计划、任务卡与每轮测试报告属于本地开发记录，未随仓库分发。

---

## 附：许可与第三方组件

- NeoVoiDrive 本体以 **GNU Affero General Public License v3.0（AGPLv3）** 发布，许可全文见仓库根目录 `LICENSE`。字体部分使用 SIL OFL 1.1，不需要字体 EULA；完整第三方清单见 `docs/THIRD_PARTY_LICENSES.md`。
- **Google Sans Code**（嵌入字体）：SIL Open Font License 1.1，Copyright 2025 The Google Sans Code Project Authors；许可全文见 `LICENSES/GoogleSansCode-OFL.txt`，随安装包分发。字体为原样嵌入，未修改/重命名；Google/Google Sans 商标归 Google LLC，本项目不使用其暗示背书。
- **JUCE 9.0.0**：双许可（AGPLv3 或 JUCE 商业许可）。本项目按 AGPLv3 使用 JUCE；闭源分发须另行持有有效的 JUCE 商业许可，并随包附带相应许可文本。
- JUCE 提供插件框架、FFT 与 DSP 基础设施；Formant 的包络搬移逻辑由 NeoVoiDrive 自有代码实现。
