# SPEC.md — NeoVoiDrive 产品规格

> 技术决策依据见 DECISIONS.md；实现结构见 ARCHITECTURE.md；开发拆分见 TASKS.md；验证方法见 TESTPLAN.md。
> 文档语言中文，标识符/参数 ID 英文（A6）。

---

## 1. 产品目标

NeoVoiDrive 是一款**语音学风格人声质感效果器**（VST3 插件），核心价值：

1. **Jitter**：无 F0 检测的音高快速随机扰动（语音学 jitter 的听感等价物），通过平滑随机源驱动可变分数延迟线实现，零算法延迟
2. **Shimmer**：无 F0 检测的振幅快速随机扰动，零算法延迟
3. **Harmonic Exciter**：谐波激励，增加高频空气感/存在感
4. **Vibration**：规则音高调制，波形支持 Sine / Square / FullWave `|sin(θ)|` / Noise，调制率 10–300 Hz
5. **Formant Shift**：全局共振峰偏移（基于 STFT 频谱包络搬移），在不改变基频与时长的前提下改变音色（年龄/体型感）
6. **多频段处理**：1–3 个频段，Jitter / Shimmer / Harmonic / Vibration 可按频段独立施加；Formant Shift 为全局效果（不属于任何频段）
7. **尽可能低延迟**：非 formant 路径接近零延迟；formant 路径延迟如实报告
8. **浅色、美观、现代、易用的 UI**

目标用户：人声后期、配音/有声内容、角色音色设计场景。

## 2. 非目标

以下内容**不属于** v1 范围：

- 任何形式的 F0/基频检测（**例外**：Sideband 模块内部使用的 MPM/NSDF F0 跟踪，仅服务于 sideband 湿路径调制，见 §4.3；以及 T66 全局 Growl 复用同一个 F0Tracker 作为控制信号；jitter / shimmer / vibration / formant 保持无 F0 设计不变，DECISIONS R17/R54）、音高纠正（auto-tune）、和声化（harmonizer）
- AU / AAX / CLAP 格式；macOS / Linux 安装包（代码保持跨平台可移植，A1）
- 每频段 solo / mute（v2 候选；per-band gain 与 Band On 已于 T37/T38 实现，见 §3 F13、§4.3）
- MIDI 学习/控制、参数调制矩阵（v2 候选）
- 线性相位分频模式、> 2x 过采样档位
- 频谱冻结、反向、粒子化等实验效果
- 内置 limiter / 自动增益补偿（formant Normalizer 默认关闭，DECISIONS §2.3）
- 多语言 UI（v1 仅英文 UI）

## 3. 功能列表

| ID | 功能 | 说明 | 对应 UI 区域 |
|----|------|------|-------------|
| F1 | 多频段分频 | 1–3 段可选；分频点 40 Hz–18 kHz 对数调节；Slope 12/24/36/48/60/72 dB/oct（LR2/LR4/LR6/LR8/LR10/LR12，T37 扩展）；实时频谱视窗（T70：音频频谱纵轴 **0…−72 dBFS**，三分频指示包络纵轴 **+18…−18 dB**；细线低透明度网格按 −18…+18 dB 绘制、不标纵轴单位）+ 分频点拖拽 | 左栏频响区 |
| F2 | Jitter（每频段） | 随机音高扰动：depth（cents）+ rate（Hz）；零延迟 | 效果卡片 ×3 |
| F3 | Shimmer（每频段） | 随机振幅扰动：depth（%）+ rate（Hz）；零延迟 | 效果卡片 ×3 |
| F4 | Harmonic Exciter（每频段） | 高通 + waveshaper + 2x oversampling（IIR half-band）；drive + tone | 效果卡片 ×3 |
| F5 | Vibration（每频段） | 规则音高调制：waveform（Sine/Square/FullWave/Noise）+ rate 10–300 Hz + depth 0–200 cents | 效果卡片 ×3 |
| F6 | Formant Shift（全局） | 专用 STFT 频谱包络搬移（T68，自研，无第三方 DSP 依赖）：log 幅度 → cepstral lifter → 包络按 `f/r` 搬移 → 保留原相位 → OLA；150–5500 Hz 带限 + 1 kHz 交叉淡化 + 帧能量归一；shift ±12 semitones；quefrency 可调（Settings，默认 0.5 ms）；延迟 2048 samples 动态报告（T12 实测修正，T68 保持） | Master 区（右栏底部）+ Settings |
| F7 | Dry / Wet + Output Trim | 全局干湿混合 0–100%；输出微调 ±12 dB | Master 区（右栏底部） |
| F8 | Bypass | 全局旁通（约 5 ms 斜坡防 click） | 顶栏 |
| F9 | Preset | 工厂预设（**17 个**，Preset 菜单按 **Voice Drive**（Growl 1/2/3 + Anger 1/2/3）/ **Sound Effects**（其余 11 个）分组显示，T69；8 个 legacy 人声预设 + 3 个 Sideband 展示预设 + Growl 1/2/3（Legacy 频段调制三档）+ Anger 1/2/3（全局 N3 三档）；按完整快照加载——先恢复参数默认值再应用预设，并自动设置所需 `band_count`）+ 用户状态完整保存/恢复（DAW session）+ **自定义预设导入/导出**（T42：UTF-8 XML `.nvpreset`；用户预设目录 `<userAppData>/NeoVoiDrive/Presets/`；Preset 菜单内联 User Presets / Export / Import / Show Preset Folder） | 顶栏 |
| F10 | Meter + Latency 指示 | 输出 L/R 峰值电平表（`MeterSource`，输出峰值 tap；T66 起固定在 Growl/Master 卡底部）；当前报告延迟（samples + ms）T66 起移至 Settings → Monitor 行 | Growl/Master 卡底部 + Settings |
| F11 | Settings | formant quefrency（0.5–5.0 ms，默认 0.5 ms）；framesize 档位（T68 固定 2048 点 STFT，UI 显示为禁用态）；**UI Size（T41/T63：75–150 %，默认 100 % = 1020×612；1200×720 设计画布统一缩放，按用户持久化，非 APVTS 参数；T72：Settings 弹窗与所有弹出菜单同步缩放）**；About 信息（T39 起使用系统原生标题栏）。T62 起 **Engine 下拉隐藏**（`engine_version` 参数仍保留，见 §4.2） | 顶栏弹窗 |
| F12 | 频段卡片灰显 | bandCount < 3 时，未启用卡片置灰禁用；参数保留不丢失（R12） | 效果卡片区 |
| F13 | 每频段 Band Gain / Band On | 每频段输出增益 −48…+12 dB（T37，左栏频响视窗下方旋钮）；**T71 非线性响度手感：50% = 0 dB、100% = +12 dB、0% = 静音（−∞，底层存 −48 dB 哨兵），偏离默认值越远变化越快**；Band On 总开关（T38，5 ms 直通旁路交叉淡化，关时整卡灰显）；两者均为每频段第 10/11 个参数（§4.3） | 效果卡片 + 左栏频响区 |
| F14 | Sideband Roughness（每频段，T40/T58） | 替代 F4 的听感角色（legacy 引擎仍保留）：F0 跟踪 PM 边带引擎，湿路径参数 Depth/Ratio/Rough/Low Cut/**Bright**；Bright 为与 Roughness 正交的线性亮度轴（1.5 kHz 高频倾斜 + 3.2 kHz 临场钟形，作用于频段输出 dry+wet，随 sideband 在电路 50 ms 渐入；默认 0 = 逐位透明）；Roughness 为恒包络相位雾轴（T58/T59：Hilbert 正交对 + 20–200 / 500–3000 Hz 随机相位控制，无音频噪声源、无 AM、无延迟线；见 SIDEBAND_SPEC §10）。**T62：Sideband UI 暂时隐藏（Band 卡不再显示 Sideband 区，Settings 隐藏 Engine 下拉），但参数、DSP、自动化与 3 个 Sideband 工厂预设全部保留；恢复 UI 不需要改参数/DSP** | 参数/DSP 保留；UI 暂停 |
| F15 | Growl（全局，T66） | 分频/Formant 之后的全局 F0 跟踪幅度调制 + 轻度去刺 EQ（N3 验收版）：Amount、Rate（fm/F0）、Tone；复用 Sideband 同一 F0Tracker，清音门控，不做自动增益；Amount=0 逐位透明；不新增延迟/PDC。UI：Master 卡内 Growl（左/默认）与 Master 两页签，底部仅 L/R 表；latency 移至 Settings | Master 卡（右栏底部） |

## 4. 参数列表

### 4.1 参数命名与通用约定

- 参数 ID（APVTS）：snake_case，每频段参数带 `b1_` / `b2_` / `b3_` 前缀
- 所有连续参数均可自动化；bool/choice 参数按宿主离散自动化
- "平滑"列：音频线程侧参数平滑时间常数（控制信号域），防止 zipper noise
- 所有频段效果参数在 depth = 0 / drive = 0 时该效果等效关闭（无独立 per-effect bypass 参数）
- **例外（Sideband，IMPACT_ANALYSIS C4）**：`{n}_sideband_enable` 为独立硬开关 —— 它是 F0 检测器 CPU 的激活条件之一与 A/B 对比手段；默认 on（depth=0 已保证静默，避免「开了 depth 没声音」）

### 4.2 全局参数

| 参数 ID | 显示名 | 类型 | 范围 | 默认 | 平滑 | 说明 |
|---------|--------|------|------|------|------|------|
| `bypass` | Bypass | bool | off/on | off | 5 ms 输出斜坡 | 全局旁通，顶栏 |
| `band_count` | Bands | int choice | 1 / 2 / 3 | 1 | 5 ms 输出淡出/淡入（结构切换） | 频段数量，分频区单选钮；切换时输出在 5 ms 内淡出、在零点切换路由并清空隐藏频段的延迟历史、再 5 ms 淡入（审查 P2-3） |
| `xover1_freq` | Crossover 1 | float, log | 40–18000 Hz | 200 Hz | 20 ms + 系数交叉淡化 | 第 1 分频点（低/中），分频区拖拽 |
| `xover2_freq` | Crossover 2 | float, log | 40–18000 Hz | 4000 Hz | 20 ms + 系数交叉淡化 | 第 2 分频点（中/高）；运行时钳制 xover1 < xover2 |
| `xover_slope` | Slope | choice | 12 / 24 / 36 / 48 / 60 / 72 dB/oct | 24 | 交叉淡化 | LR2 / LR4 / LR6 / LR8 / LR10 / LR12 |
| `formant_enabled` | Formant On | bool | off/on | off | 5 ms 输出交叉淡化 | formant 总开关；切换时动态 setLatencySamples（A2） |
| `formant_shift` | Formant Shift | float | −12 … +12 semitones（0.1 st 步进） | 0.0 | 帧边界控制平滑 | 包络倍率 `r = 2^(st/12)`（T68）；Master 区旋钮 |
| `formant_quefrency` | Quefrency | float, log | 0.5–5.0 ms | **0.5 ms**（T68） | 帧边界控制平滑 | cepstral lifter 截止；Settings 弹窗 |
| `dry_wet` | Dry / Wet | float | 0–100 % | 100 % | 10 ms | 全局混合，Master 区 |
| `output_trim` | Output Trim | float | −12 … +12 dB（0.1 dB 步进） | 0.0 dB | 10 ms | 输出微调（补偿激励/闪烁引入的响度变化），Master 区 |
| `engine_version` | Engine | choice | `Legacy (Harmonic Exciter)` / `Sideband Roughness` | **Legacy (Harmonic Exciter)**（T62 恢复；T35 曾翻转到 Sideband） | 5 ms 输出交叉淡化 | 新旧引擎运行时 feature flag（REFACTOR_PLAN §3）。`ParameterID(id, 2)`；切换仅块边界生效，报告延迟不变。T62：Settings 不再显示 Engine 下拉；参数保留，供旧 v2 工程、DAW 自动化与 3 个 Sideband 工厂预设显式选择 Sideband。v1 工程状态仍强制 Legacy（R18/R20） |
| `growl_amount` | Growl Amount | float | 0–100 % | **0 %** | 20 ms | T66 全局 Growl 深度；0 = 逐位透明（AM/EQ 全部跳过）；复用 F0Tracker；Growl UI 页签 |
| `growl_rate` | Growl Rate | float | 0.15–0.30（0.001 步进） | **0.20** | 20 ms | fm = rate × 跟踪 F0；显示为 0.20× |
| `growl_tone` | Growl Tone | float | −100 … +100（1 步进） | **0** | 20 ms | 0 = N3（−3 dB @4 kHz Q1.2 + −1 dB @8 kHz）；负值更亮、正值更暗；与 Amount 共同缩放 |

### 4.3 每频段参数（`{n}` ∈ `b1`/`b2`/`b3`）

> 下表“显示名”是不带频段前缀的基础名；**宿主自动化列表中的实际显示名带 `Band N ` 前缀**（如 `Band 1 Jitter Depth`），三个频段不再重名（2026-11-03 审查 P3）。

| 参数 ID | 显示名 | 类型 | 范围 | 默认 | 平滑 | 说明 |
|---------|--------|------|------|------|------|------|
| `{n}_jitter_depth` | Jitter Depth | float | 0–1200 cents | 0 | 20 ms | 随机音高扰动峰值（SPEC §5 容量按 1200 ct 最坏情况设计） |
| `{n}_jitter_rate` | Jitter Rate | float, log | 10–1000 Hz | 100 Hz | 20 ms | 随机源更新率（噪声带宽） |
| `{n}_shimmer_depth` | Shimmer Depth | float | 0–100 % | 0 | 20 ms | 随机振幅扰动深度 |
| `{n}_shimmer_rate` | Shimmer Rate | float, log | 10–1000 Hz | 100 Hz | 20 ms | 随机源更新率（独立实例） |
| `{n}_harmonic_drive` | Harmonic Drive | float | 0–100 % | 0 | 10 ms | waveshaper 驱动量（含输出补偿） |
| `{n}_harmonic_tone` | Harmonic Tone | float, log | 1000–10000 Hz | 3000 Hz | 20 ms | 激励高通频率（只激励高频段） |
| `{n}_gain` | Band Gain | float | −48 … +12 dB | 0 dB | 20 ms | 每频段响度旋钮（频谱视窗下方；包络显示随增益更新）。**T71 非线性映射**：归一化 0.5 → 0 dB、1.0 → +12 dB、0 → 静音（−∞；物理值存 −48 dB 哨兵，DSP 门控为精确 0）；衰减 `0.08|d|+0.92|d|³`、提升 `0.80d+0.20d³`，`d=(x−0.5)/0.5`。工程/预设保存物理 dB，声音不变；DAW 归一化自动化曲线会按新映射解释 |
| `{n}_enable` | Band On | bool | off/on | on | 5 ms 旁路交叉淡化 | Band Card 总开关：off 时该频段全部效果旁路（直通），整卡变灰 |
| `{n}_fog`（T49；T50 显示名） | Rough | float | 0–100 % | **0** | 20 ms | **整段相位雾**（Band Effects 的 Rough，原显示名 Fog；参数 ID 保留 `_fog` 以兼容自动化）：频段链末端（引擎混合之后）对整个 band 输出做**恒包络相位旋转**，与 Sideband Rough 共用 **FogCore**（T51/T58/T59/T60），仅作用范围不同。T58–T60 的相位控制为 20–200 Hz 主带 + 500–3000 Hz 细节带（0.70/0.30，R100 4 rad RMS；T60 折中：比 T58 细，比 T59 少远侧带雾层），由 Hilbert 正交对旋转；R50 中心线约 32 %（R100 才接近全雾），能量成为围绕谐波的宽峰，不是“细线 + 底雾”。控制 RNG 不进入音频，**没有附加噪声源/AM**；无延迟线，按 0 报告延迟；Legacy/Sideband 引擎均生效；0 = 逐位透明（R21/R44/R50） |
| `{n}_vib_wave` | Vib Wave | choice | Sine / Square / FullWave / Noise | FullWave | — | FullWave = \|sin(θ)\|（Q1） |
| `{n}_vib_rate` | Vib Rate | float, log | 10–300 Hz | 10 Hz | 20 ms | 调制率；Noise 时为噪声带宽 |
| `{n}_vib_depth` | Vib Depth | float | 0–200 cents | 0 | 20 ms | 规则音高调制深度 |
| `{n}_sideband_enable` | SB On | bool | off/on | **on** | —（经 depth 平滑联动） | 硬开关：off 时该频段湿路径强制旁路、F0 检测器激活条件之一。默认 on 配合 depth=0，避免「拉 depth 无声」（IMPACT_ANALYSIS C4）。**T62：Sideband UI 暂停，参数/预设/自动化保留** |
| `{n}_sideband_depth` | SB Depth | float | 0–100 % | 0 | 10 ms | 调制深度：同时映射 `βref ∈ [0, 2]` 与 wetGain。0 = 完全透明（§4.1 惯例不变） |
| `{n}_sideband_fm_ratio` | SB Ratio | float | 0.000–0.500（0.001 步进） | 0.333 | 20 ms | `fm / f0` 比值；默认 1/3（非整数感区间，边带间距宽） |
| `{n}_sideband_roughness` | SB Rough | float | 0–100 % | 20 % | 20 ms | 恒包络相位雾：湿路径 PM 之后接共享 **FogCore**（T51/T58/T59/T60；Hilbert 正交对 + 20–200 Hz 主相位控制 + 500–3000 Hz 细节控制，0.70/0.30，R100 4 rad RMS）；只旋转湿信号的解析相位，无音频噪声源/AM，无额外延迟。Sideband 额外保留 pn 相位域 ±0.45 rad（±700–1400 Hz）与固定 Lmax（含历史 F 预留）。只作用于 sideband 湿路径，dry 保持满电平；Rough=0 时湿路径为干净读点（SIDEBAND_SPEC §10 / R39、R40、R45、R50） |
| `{n}_sideband_lowcut` | SB Low Cut | float, log | 100–8000 Hz | 2000 Hz | 20 ms + 系数交叉淡化 | 湿路径高通截止；以下保持纯 dry（基频附近干净） |
| `{n}_sideband_bright` | SB Bright | float | 0–100 % | **0** | 20 ms（+ 50 ms 在电路渐入） | 与 Roughness 正交的线性亮度轴（T40）：频段输出（dry+wet）经 1.5 kHz 一阶高频倾斜（+6 dB/oct）+ 3.2 kHz 临场钟形（Q=2，0 dB 峰 BPF 加法提升）。不改雾化、不加 waveshaper、不改延迟；Bright=0 = 逐位透明且零额外 CPU（SIDEBAND_SPEC §10.1） |

**参数合计**：全局 10 个 + 每频段 11 个 × 3 + 每频段 sideband 6 个 × 3 + `engine_version` 1 个 + 每频段 fog 1 个 × 3 + T66 Growl 3 个 = **68 个**（T39 TEMP debug 参数已于 T46 移除；T49 新增 3 个整段雾化参数；T66 新增 3 个全局 Growl 参数）。

> **两个 Rough 的一致性（T51/T58/T59/T60 / R45、R50）**：`{n}_fog`（Band Effects）与 `{n}_sideband_roughness`（Sideband）使用同一个 `dsp::FogCore`：相同的 8 段全通 Hilbert 正交对、20–200 Hz 主相位控制、500–3000 Hz 细节相位控制、0.70/0.30 混合与 4 rad RMS @R100；输出 `i·cos(phi) − q·sin(phi)` 是酉相位旋转，**没有音频噪声源、没有 AM、没有延迟线**。唯一差异是作用范围：Band Effects 在引擎混合之后处理整个 band；Sideband 只处理湿路径，dry 始终保持满电平。Sideband 的周期 PM、pn、voiced 门控、wetGain、lowcut、Bright 属于其载波/湿路机制，不在共享雾核内。验收在 **Rough 30–50** 的“展宽但谐波仍在”区间；Rough 100 是全雾终点。T60 实测（Band Effects，1 kHz）：R30/R50 总 RMS +0.01/+0.02 dB、中心线 ±5 Hz 能量 86.11/32.02 %、±100 Hz 内能量 95.40/69.84 %；100 Hz 中心线 88.7/40.0 %、±100 Hz 内能量 97.5/81.0 %；R30/R50 10 ms 包络 CV 0.022/0.029；Sideband wet-solo 1 kHz R0/R50 线幅 0.201/0.114、5–20 Hz 包络占比 0/0 %（无新增音量抖动）。

> 布局注册顺序（IMPACT_ANALYSIS C5 / T45 复核）：10 个全局参数 [0..9] → 3 × 11 legacy band [10..42]（原 9 个 + T37 `gain` + T38 `enable`）→ 3 × 6 sideband [43..60]（b1→b3，每段 enable/depth/fm_ratio/roughness/lowcut/bright）→ `engine_version` [61] → 3 × 1 band fog [62..64]（b1→b3；T49 保持既有索引稳定，追加在末尾）→ 3 × Growl [65..67]（T66 同样追加在末尾）（共 68 个参数）。原始 v1 的 37 个参数保持相对顺序，当前 legacy 前缀为 **43**。全部 T37 之后新增参数使用 `juce::ParameterID(id, 2)`（含 gain/enable/sideband/engine/fog）；JUCE 9 VST3 默认按参数 ID 字符串哈希生成 ParamID，因此该顺序不会移位当前 VST3 自动化 ID；若未来构建 AU 或启用 legacy ParamID，必须保持 versionHint 单调（DECISIONS R19/R41/R44）。

### 4.4 参数边界规则

- `xover2_freq` 实际生效值 = `max(xover2, xover1 × 1.05)`（参数层不改动用户设定值，DSP 层钳制）
- depth = 0 的效果在 DSP 中不产生湿声处理（昂贵湿路径跳过；基础平滑/延迟线仍运行以保持流对齐，R9 的语义为“湿声旁路”）
- `band_count` 减少时被隐藏频段的参数值保留于 APVTS 状态（R12）
- 300 Hz 高调制率 + 大 depth 会产生 FM 边带（预期行为，R4）；原计划的 `vib_rate > 100 Hz` 旋钮颜色渐变提示 **v1 未实现**（仅保留行为本身，不阻止参数）
- Sideband 专属边界（SIDEBAND_SPEC §4.3）：
  - depth = 0 → 该频段湿路径零 CPU（复用 R9 惯例：跳过 PM/噪声计算，dry 延迟匹配仍推进）
  - `{n}_sideband_enable` = off 等价于 depth 强制为 0（T62 起 Sideband UI 暂停；参数值保留，预设/自动化仍可设置）
  - `fm_ratio` × f0 的有效 fm 钳制在 [1 Hz, 8 kHz]（防 192 kHz 下极端组合发散）
  - `{n}_sideband_bright` > 0 在 sideband 模块"在电路"时生效（`enable` 且 depth > 0，**不含 voiced 判定**：清辅音/换气不产生音色抖动），50 ms 渐入；Bright = 0 时亮度级整体跳过、滤波器状态清零（零额外 CPU、逐位透明）
  - Bright 作用于频段输出（含 dry）：频段本身在 1.5 kHz 以上有内容才可感（b1 通常几乎无感，b2/b3 为主战场；lowcut 不限制 Bright——它提升的是输出，不是只抬湿层）；Sideband UI 暂停（T62），恢复时仍不做联动钳制（SIDEBAND_SPEC §10.1）
  - Bright 不改变 `L(depth)` 与报告延迟（§5.1 不受影响）；Bright=100 时 3–8 kHz 最多约 +10 dB，峰值随素材 HF 密度上升，必要时用 Band Gain / Trim 回收
  - `engine_version` 切换只在块边界生效 + 5 ms 交叉淡化（同 `formant_enabled` 模式，DECISIONS A2）
  - `{n}_fog`（T49；UI/宿主显示名 **Rough**，T50）：`0` = 逐位透明；作用点为频段链末端（引擎混合之后），因此不依赖 `engine_version`、`sideband_enable` 或 `sideband_depth`；T58 起是**无延迟线的恒包络相位雾**，不纳入 PDC 报告值（R21/R44/R50）；Band On = off 时随整个效果链旁路。与 Sideband Rough 共用 FogCore，但作用范围是整段（scope 规则见 §4.3 注）。
  - Sideband Rough 只作用于湿路径，dry 满电平；即使 Rough=100，输出仍是 dry + wet，这是 scope 差异而非雾核差异。
  - `{n}_fog` 在 `band_count` 隐藏频段上仍保留 APVTS 值（R12）；隐藏频段**不参与** `processChain()`（其延迟线/滤波器历史冻结）；重新启用时在输出淡出零点清空该链状态，避免重放旧延迟内容（审查 P2-3）。

### 4.5 参数迁移（state v1 → v2，REFACTOR_PLAN §4.2 / §4.3）

- 状态 `version` 自 v1.1 起写 **"2"**，APVTS 状态含全部 **68** 参数（T46 起 62；T49 +3 fog；T66 +3 Growl；`getStateInformation`）。
- **v1 状态**（无 `version` 属性或 `"1"`，仅含 37 个旧参数）载入流程（`setStateInformation`）：37 个旧参数原位恢复 → **31 个新参数**（18 个 sideband 含 Bright + 6 个 gain/enable + `engine_version` + 3 个 fog + 3 个 Growl）落布局默认（Growl 默认 0，透明）→ **随后强制 `engine_version = Legacy`**（消息线程 `setValueNotifyingHost`）→ 旧工程声音逐位不变，`harmonic_drive/tone` 继续生效（DECISIONS R20）。
- `version="2"` 状态直接按存储值恢复；未来版本（>2）按容错规则加载（未知参数忽略、缺失取默认）。
- 工厂预设（T30 起）全部显式携带 `engine_version` 字段，加载结果与加载顺序无关。
- **听感近似映射**（供手动参考与预设重调起步，**不自动执行、不做等效承诺**；T35 人工重调）：

| 旧（harmonic） | 新（sideband）建议初值 |
|----------------|----------------------|
| `drive` 0–100 % | `sideband_depth` 同值起步 |
| `tone` 1000–10000 Hz | `sideband_lowcut` ≈ tone × 0.5，钳入 [100, 8000] |
| —（无对应） | `sideband_fm_ratio` = 0.333、`sideband_roughness` = 20 %（模块默认） |

## 5. 延迟预算

### 5.1 报告延迟（setLatencySamples）

| 状态 | 报告延迟 | 44.1k | 48k | 88.2k | 96k | 176.4k | 192k |
|------|---------|-------|-----|-------|-----|--------|------|
| `formant_enabled` = off | **0 samples** | 0 ms | 0 | 0 | 0 | 0 | 0 |
| `formant_enabled` = on | **2048 samples** | 46.44 ms | 42.67 ms | 23.22 ms | 21.33 ms | 11.61 ms | 10.67 ms |

- **Growl（T66）**：作用于湿路的乘性 AM + 因果 biquad，无延迟线，**不改变报告延迟/PDC**；Amount=0 时逐位透明。T66 的 F0 仅用于控制率（复用 F0Tracker），不进入音频路径。
- **默认引擎（T62）**：新实例 `engine_version = Legacy`，因此默认报告 **0 samples**（formant 关）/ **2048 samples**（formant 开）。只有显式选择 Sideband（旧 v2 工程、DAW 自动化或 3 个 Sideband 工厂预设）才进入下面的 Lmax 行；Settings 不提供 Engine 下拉。
- 切换时调用 `setLatencySamples` + 约 5 ms 输出交叉淡化（A2 / R3）
- **过渡态对齐（2026-11-02 审查 P1-1）**：`formant_enabled` 打开的那一块立即把报告值改为 2048(+Lmax)，同时 dry 与 formant 交叉淡化的基信号切换到**延迟 tap**；随后 2048 样本的 hold 期（formant 引擎尚未产出有效帧）输出的是延迟 2048 的 dry，而不是未延迟路径。这样 hold 窗口与宿主 PDC 全程一致（L05 实测保持窗口脉冲 = 2048）。hold 结束后的 5 ms 淡入在两条已对齐的信号之间进行。关闭时报告值立即回落、dry 立即回到 0 tap；formant 湿路在 5 ms 淡出窗口内仍带 2048 延迟，这是唯一接受的亚块级例外。
- 报告值在 `prepareToPlay`（强制发布）、状态/预设加载（消息线程 `refreshLatencyReport`）与每块 `processBlock` 都会刷新；复用实例重 prepare 不会残留上一次的 53/2048/2101（审查 P1-2）。
- 谐波激励的 2x oversampling 使用 polyphase IIR half-band，群延迟 < 10 samples（非整数），按 0 报告；此折衷已在 DECISIONS §3.2 / R11 显式声明，并由 TESTPLAN §3 实测留档
- **Sideband 声明行（SIDEBAND_SPEC §9.1，IMPACT_ANALYSIS C8；T45 固定 Lmax）**：sideband 湿路径基线使用**固定最大延迟** `Lmax = A(100%) + F + 4`，其中 `A(100%) = 2/(2π·1000)·sr`、`F = round(34·sr/48000)` 样本。`engine_version=1` 时报告 **Lmax / 2048+Lmax**，与 depth、Band On、SB On、band_count 均无关（depth 只改变固定窗口内的调制幅度）；Legacy 仍为 0 / 2048。`Lmax` 随采样率：**48 kHz 53 样本（1.10 ms）、44.1 kHz 49、96 kHz 103、192 kHz 201**（SB-L01 实测）。所有未处理路径（全局 Dry/Wet 的 dry、Bypass、Band On 关闭的 dry）都延迟到同一报告值，因此 Dry/Wet、Bypass 与多频段求和始终时间对齐；depth 自动化不再改变报告值，避免宿主 PDC 反复重补偿。代价是 depth=0 的默认报告从 T44 的 38 样本增至 53（+0.31 ms @48k）；depth=100 与 T44 相同（53）。`F` 必须计入基线；`Lmax` 取整用于宿主报告与整数 tap，内部调制窗口保留小数 A。`engine_version` 切换时报告值会改变（0/2048 ↔ Lmax/2048+Lmax），按 A2 同款处理：块边界生效 + 5 ms 交叉淡化，宿主可能重补偿一次（与 formant 开关同性质）。F0 检测滞后约 31 ms 仅在控制域（fm），不进音频路径
- **Band Rough（T49/T50，R44/R50）**：`{n}_fog`（UI 显示名 Rough，原 Fog）T58 起是**无延迟线的恒包络 Hilbert 相位雾**，按 0 报告延迟（R21/R44/R50）；Rough>0 时只旋转 band 的解析相位，不改变 `setLatencySamples`，也不改变 dry/Bypass/PDC 值；Rough=0 时逐位透明；控制 RNG 不进入音频

### 5.2 内部缓冲预算（预分配依据）

| 缓冲 | 容量依据 | 容量（@192 kHz 上限） |
|------|---------|----------------------|
| 音高调制延迟线（每声道×每频段） | 最坏情况：10 Hz × 1200 cents → A ≈ 15.9 ms；×1.5 余量 + 插值 4 点 | ≥ 24 ms → **4608 samples**（取 8192 对齐） |
| Formant 输入环形缓冲（T68） | fftSize 2048 + maxBlockSize + hop 512 + 64 | 2048 + 8192 + 512 + 64 = **10816 samples** |
| Formant 输出 OLA 缓冲（T68） | fftSize 2048 + latency 2048 + maxBlockSize + hop 512 + 64 | 2048 + 2048 + 8192 + 512 + 64 = **12864 samples**（消费后清零，防环回重放） |
| 分频/求和 work buffer | maxBlockSize × 声道数 × 频段数 | 8192 × 2 × 3 |
| 频谱 FIFO（UI） | ≈341 ms 单声道抽点（16384 @48 kHz-equivalent） | 固定 16384 samples（lock-free） |
| Sideband Bright 滤波器（每声道×每频段） | 一阶 LP + 一级 0 dB 峰 BPF（TDF-II double state），无新增缓冲 | —（仅 Bright > 0 时计算） |
| Band Rough（每声道×每频段，T49/T58） | T58 起为无延迟线的 Hilbert 相位雾；常量滤波器状态 + FogCore 控制 RNG | 无额外延迟线/缓冲 |

## 6. 支持的采样率与 Block Size

| 项 | 规格 |
|----|------|
| 采样率 | 44.1 / 48 / 88.2 / 96 / 176.4 / 192 kHz 为支持范围（`prepareToPlay` 时全模块重算系数与容量）；插件**信任宿主采样率**、不做内部重采样/假回退；< 32 kHz 或 > 384 kHz 时记录告警并按宿主率运行（质量不承诺）；**192–384 kHz** 按宿主率运行但记录“超出质量范围”告警（延迟线容量/F0 窗口只保证到 192 kHz，深度超限时 `ModDelayLine` 钳位）；`offline_render` CLI 在入口拒绝 **>192 kHz**（审查 P2-6） |
| Block size | **1–8192 samples，任意值、运行中可变**；所有内部处理块大小无关（formant 经环形缓冲适配层，DECISIONS §2.3） |
| 声道布局 | 立体声（主总线）；单声道输入宿主按单声道处理（每声道独立延迟线/RNG，R6）；不支持环绕声布局 |
| 采样精度 | 内部 float 处理；biquad 系数与状态 **double**（Crossover/Harmonic/Sideband，R7/T47）；formant STFT 为 **float**（T68：JUCE `juce::dsp::FFT` float，cepstral 包络/增益也以 float 计算；最终输出 float） |

## 7. 平台与格式

| 项 | 规格 |
|----|------|
| 首要平台 | Windows 10 / 11，x64 |
| 插件格式 | **VST3**（`.vst3` bundle，**唯一发行格式**）+ **Standalone**（本地调试/离线测试构建，不随发行包分发）（Q2） |
| 编译器 | MSVC 2022（v143）及以上，C++20（`/std:c++20`） |
| 构建系统 | CMake ≥ 3.22；JUCE 9.0.0 优先使用本地 `stuff/JUCE`，否则经 FetchContent 拉取 tag 9.0.0 |
| JUCE 模块 | juce_core / juce_events / juce_graphics / juce_gui_basics / juce_audio_basics / juce_audio_devices / juce_audio_formats / juce_audio_plugin_client / juce_audio_processors / juce_audio_utils / juce_dsp / juce_data_structures |
| 第三方 DSP | 运行时与构建均无第三方 DSP 依赖（T68 起 Formant 为自研 STFT 包络搬移）；golden 测试素材 `tests/assets/voice.wav` 独立存放，不随发行包分发 |
| 插件元数据（暂定） | Name: `NeoVoiDrive`；Vendor: `M.R.V.D.`（About: `NeoVoiDrive alpha 0.0.1 by MirroredVoid`）；Bundle ID: `com.neovoi.neovoidrive`；Version: 0.0.1（CMake 数字版本；显示为 `alpha 0.0.1`）；Category: `Fx` / `Vocal`；执行期可调整 |
| 目标 DAW 测试 | Reaper（首要）、FL Studio（次要）（TESTPLAN §5） |

---

*版本：v2.27（2026-11-08 T75：发行包仅含 VST3；Standalone 保留为本地调试/离线测试构建，不随包分发）；v2.26（2026-11-07 依赖口径清理：golden 素材移至 `tests/assets/voice.wav`；构建/运行/测试均不依赖 stftPitchShift，SPEC 不再以该库作为当前依赖）；v2.25（2026-11-05 T74：Fog/Sideband/Growl 的每样本 sin/cos/tanh 改用 JUCE Pade 近似（相位归一到 ±π、tanh 钳 ±5），完整链路 A/B 无可闻差异；P01 formant ON 3.93→2.76–2.95 %；332 用例/60 suite）；v2.24（2026-11-03 T73：插件元数据 Version 0.1.0 → **0.0.1**（About 显示 `alpha 0.0.1`）、Vendor NeoVoi → **M.R.V.D.**；界面左右栏 65/35 → **70/30**（左 812 / 右 348，画布/默认尺寸不变）；Band 页签文字改为在扣除开关后的文本区内居中，避免窄右栏重叠；329 用例/60 suite）；v2.23（2026-11-03 T72：Settings 弹窗与弹出菜单随 UI Size 缩放；329 用例/60 suite）；v2.22（2026-11-03 审查 P3 追加：HarmonicExciter tone 系数 32 样本控制速率刷新、频段参数显示名加 Band N 前缀、ParamSnapshot 非有限值归零；327 用例/60 suite）；v2.21（2026-11-03 审查 P3 清理：状态根校验/判空、首块 gain/enable snap、About 由构建宏生成、VST3 分类 Fx|Vocal、tail 0.05 s、dry-ring 钳位、页签保留选中索引；325 用例/60 suite）；v2.20（2026-11-03 审查 P1/P2 修复：formant hold 从 enable 块起与实际输出对齐（hold 输出 2048 延迟 dry）；延迟在 prepare/状态与预设加载时强制发布；slope 切换旧/新 bank 20 ms 交叉淡化；移除 FormantEngine 的 /arch:AVX2（全工程 SSE2）；band_count 切换 5 ms 淡出/零点切路由/清空延迟历史/5 ms 淡入；F0 hold 的 600 ms 超时不再受滞回带影响；Growl Amount=0 跳过控制路径；频谱只取最新 FFT 块并排空隐藏源；Settings 显示 2048；离线 CLI 拒绝 >192 kHz；分频 UI 采用 1.05× 钳制并按 band_count 禁用无效控件；322 用例/60 suite、ctest 2/2）；v2.19（2026-11-02 T71：Band Gain 三个旋钮改用已验收的非线性响度手感——50% = 0 dB、100% = +12 dB、0% = 静音（−∞，物理 −48 dB 哨兵）；衰减 0.08|d|+0.92|d|³、提升 0.80d+0.20d³；新增 `params/LoudnessCurve.h`、映射/范围/静音用例，311 用例/60 suite；参数范围/ID/数量不变，工程与预设按物理 dB 保持声音）；v2.18（2026-11-01 T70：频谱视窗采用双纵轴映射——音频频谱 **0 dBFS（顶）… −72 dBFS（底）**，三分频指示包络 **+18 dB（顶）… −18 dB（底）**；新增细线低透明度网格（横线按 −18…+18 dB、6 dB 步进，不标纵轴单位）；新增 `spectrumDbToY/envelopeDbToY` 映射用例，307 用例/58 suite）；v2.17（2026-11-01 T69：Preset 菜单工厂预设按 **Voice Drive**（Growl 1/2/3 + Anger 1/2/3）/ **Sound Effects**（其余 11 个）分组显示；底层预设顺序、参数与加载结果不变；306 用例/58 suite）；v2.16（2026-11-01 T68：Formant Shift 改为专用 STFT 包络搬移，移除 stftPitchShift 依赖；2048 点 FFT / 512 hop、0.5 ms cepstral lifter + 1.5× 对比度、150–5500 Hz 带限、原相位、帧能量归一；`formant_quefrency` 默认 1.0 → **0.5 ms**；参数仍 68、UI 不变、PDC 仍 2048；shift=0 逐位透明（核心 SNR ≈138 dB，全链 A06 31.9 → **46.5 dB**）；305 用例/58 suite）；v2.15（2026-11-01 T67：工厂预设 13 → **17**；新增 Growl 1/2/3（T61 Legacy 频段调制的轻/中/重三档，原 Growl 1 = 新 Growl 2）与 Anger 1/2/3（N3 全局 Growl 的轻/中/重三档，原 Growl 2 = 新 Anger 2）；无 DSP/参数/UI/PDC 变化）；v2.14（2026-10-31 T66：新增全局 Growl（3 参数，共 **68 参数**）；复用 F0Tracker、清音门控、Amount=0 逐位透明、零 PDC；Growl/Master 页签（Growl 左/默认，Band 页签样式）；底部仅 L/R 表、latency 移至 Settings；Growl 2 工厂预设（共 **13 个**）；302 用例/58 类、ctest 2/2）；v2.13（2026-10-30 T63：画布 **1200×720**（默认 100% = **1020×612**），左栏 **65% = 754** / 右栏 **406**；每个页签内部、Band N 文字左侧放置无文字 Band On 开关（8 px 边距），BandCard 移除头部标题行；右栏 tabs 430 + Master 188；左栏底排 90 px 旋钮格 + 72 px 显示模式；参数仍 65，DSP/ID/范围不变；v2.12（2026-10-30 T62：左右分栏 1040×760（默认 884×646）；左 600/右 400；BandCard 移除 Sideband UI、Band Effects 3 列 + Vibration wave 全宽；Master 右栏底部；默认 `engine_version = Legacy`、Settings 隐藏 Engine；频响 −60…+12 dB；参数仍 65，DSP/ID/范围不变；v2.11（2026-10-30 T61：新增 Growl 1 工厂预设（Legacy；b2 Sine 128/120 + Jitter 320/375；b3 Noise 200/150 + Rough 30 + Gain +6 dB；预设 12 个），无 DSP/参数/UI/PDC 变化；v2.10（2026-10-27 T60：Rough 相位控制主/细节权重 0.60/0.40 → **0.70/0.30**（收回 T59 过强的远侧带，消除“谐波下面垫一层雾”；相位率 588.5 Hz、<300 Hz 2.16 %，远侧带约 0.9 %）；1 kHz R30/R50 中心线 ±5 86.11/32.02 %、±100 内 95.40/69.84 %，100 Hz 88.7/40.0 %、±100 内 97.5/81.0 %；无参数/UI/延迟/PDC 变化；v2.9（2026-10-26 T59：Rough 相位控制主/细节权重 0.75/0.25 → **0.60/0.40**（细节优先，细化 R30 颗粒）；1 kHz R30/R50 中心线 ±5 86.27/32.44 %、±100 内 93.92/62.34 %，100 Hz 88.4/38.9 %、±100 内 95.6/70.4 %，总 RMS 不变；无参数/UI/延迟/PDC 变化；v2.8（2026-10-26 T58：Rough 改为 **恒包络相位雾**（Hilbert 正交对 + 20–200 / 500–3000 Hz 随机相位控制，0.75/0.25，R100 4 rad RMS）；无音频噪声源、无 AM、无延迟线；1 kHz R50 中心线 ±5 31.90 %、±100 内 72.78 %，100 Hz 40.4 %/85.2 %，总 RMS 不变；BandFog 按 0 报告延迟，Sideband Lmax 保持历史 F 预留；无参数/PDC 变化；v2.7：2026-10-26 T55：双频带 AM（已由 T58 取代）；v2.6：2026-10-26 T54：600–1200 Hz 带通 AM（已由 T55 取代）；v2.5：2026-10-26 T53：单抽头全湿、无干净 tap/无 shelf，深度 amount^1.5，验收 R30–50；v2.4：T52：双抽头 + shelf（已由 T53 取代）；v2.3：2026-10-23 T51：Band Effects Rough 与 Sideband Rough 共用 `FogCore`，统一 grain 曲线/混合律，HF residual 0.15 → 0.30；Sideband dry 保持满电平；v2.2：T50：Band Effects 的 `{n}_fog` 显示名 Fog → **Rough**（参数 ID 不变），Sideband Rough 移至该行最右；v2.1：T49：新增每频段 `{n}_fog` 整段雾化（UI 小节改名 Band Effects），65 参数；v2.0：2026-10-21 T48：采样率策略改为信任宿主 + 工具入口限制；v1.9：T47：Crossover/HarmonicExciter biquad 改 double，Formant float 口径回写；v1.8：T46：移除 T39 TEMP debug 参数，62 参数；v1.7：T45：Sideband 固定 Lmax + Dry/Bypass/Band off 对齐；v1.6：T44：雾摆幅 F 22→34、`L = A + F + 4` 延迟数值与 §4.3 Rough 说明更新；v1.5：T42：F9 增自定义预设导入/导出（`.nvpreset`，用户预设库）；v1.4：T41 Settings UI Size（75–150 %，默认 100 % = 654×799）；v1.3：T40 F14 Sideband 引擎与 `{n}_sideband_bright` 独立亮度轴、11 工厂预设、63 参数；Sideband 模块规格见 SIDEBAND_SPEC.md / DECISIONS R36）*
