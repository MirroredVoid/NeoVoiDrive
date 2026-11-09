# ARCHITECTURE.md — NeoVoiDrive 架构设计

> 产品规格见 SPEC.md；设计决策与任务拆分属于本地开发记录，未随仓库分发。

---

## 1. 目录结构

```
NeoVoiDrive/
├── README.md                       # 面向用户的插件安装/使用说明（根目录）
├── CMakeLists.txt                  # 顶层：JUCE 引入、juce_add_plugin、tests 子目录
├── docs/                           # 随仓库发布的 6 篇文档
│   ├── README.md / AGENTS.md       # 开发/构建说明 / 协作约定
│   ├── SPEC.md / ARCHITECTURE.md / USER_MANUAL.md
│   └── THIRD_PARTY_LICENSES.md
├── assets/fonts/                   # 内嵌字体 Google Sans Code（OFL 1.1）
├── samples/                        # 试听示例（before / after）
├── source/
│   ├── PluginProcessor.h/.cpp      # AudioProcessor：持有 APVTS 与全部引擎，编排 processBlock
│   ├── PluginEditor.h/.cpp         # 顶层编辑器：T62 两栏布局（Header 全宽 + 左频响 / 右 Band+Master）+ T41 缩放画布
│   ├── params/
│   │   ├── ParameterIDs.h          # 68 个参数 ID 常量（与 SPEC §4 逐字一致；T49 +3 fog，T66 +3 Growl）
│   │   ├── ParameterLayout.h/.cpp  # APVTS 布局工厂（范围/默认/步进/对数映射）
│   │   ├── LoudnessCurve.h         # T71 Band Gain 非线性响度曲线（范围/文本/DSP 共用）
│   │   └── ParamSnapshot.h         # RT 参数快照 POD struct（每块填充一次）
│   ├── dsp/
│   │   ├── RandomModSource.h       # S&H 白噪声 + one-pole 平滑；每实例独立 RNG（R6）
│   │   ├── ModLFO.h                # Sine/Square/FullWave/Noise LFO + slew 限制（R5）
│   │   ├── ModDelayLine.h          # 可变分数延迟线（4 点插值），cents↔延迟深度换算
│   │   ├── PitchModUnit.h          # Jitter+Vibration 合流：调制信号相加 → 单延迟线
│   │   ├── ShimmerUnit.h           # 随机振幅调制
│   │   ├── HarmonicExciter.h       # legacy 引擎：HP + 2x oversampling + waveshaper + mix
│   │   ├── SidebandEngine.h        # sideband 引擎：HP(lowcut) + PM 湿路径 + FogCore + 后置 HP + Bright（T40/T58）
│   │   ├── FogCore.h               # T51/T58 两套 Rough 共用的恒包络 Hilbert 相位雾
│   │   ├── TrigApprox.h            # T74 三处热点共用的 Pade sin/cos/tanh（相位归一到 ±π、tanh 钳 ±5）
│   │   ├── BandFogUnit.h           # T49 整段 Rough：FogCore 相位雾，无延迟线，0 = 逐位透明
│   │   ├── GrowlEngine.h           # T66 全局 Growl：F0 AM + 噪层 + 清音门控 + tone EQ；无延迟线
│   │   ├── F0Tracker.h             # MPM/NSDF F0 检测（Sideband + T66 Growl 共用，全局单实例）
│   │   ├── CrossoverBank.h         # 自研 LR-N 分频（LR2/4/6/8/10/12，1–3 段动态）
│   │   ├── BandProcessor.h         # 单频段装配：PitchMod → Shimmer → Exciter/Sideband → Fog
│   │   ├── MultibandEngine.h       # 分频 → 3×BandProcessor → 极性校正求和 + F0 广播
│   │   ├── FormantEngine.h/.cpp    # T68 专用 STFT 包络搬移：cepstral lifter + 带限 + OLA
│   │   └── LatencyManager.h        # 动态 setLatencySamples + 切换交叉淡化
│   ├── ui/
│   │   ├── DopaminePalette.h      # T64 唯一色板：表面/role/band/section + section/role 属性解析
│   │   ├── NeoLookAndFeel.h/.cpp   # 新拟物 + 多巴胺主题（V4 LookAndFeel 派生）
│   │   ├── TopBarComponent.*       # Logo / Bypass / Preset / Settings
│   │   ├── CrossoverDisplay.*      # 频谱视窗 + 分频点拖拽 + 段数/Slope/Gain 控件
│   │   ├── SpectrumSource.h        # 音频线程 tap → SPSC FIFO → FFT 数据供给（In/Out 双源）
│   │   ├── BandCardComponent.*     # 标签页式频段卡片（Band Effects/Vibration；Sideband UI 暂停）
│   │   ├── BottomBarComponent.*    # Master 卡：Growl（左/默认）/ Master 嵌套页签；Formant/Dry-Wet/Trim；仅 L/R 表
│   │   └── MeterSource.h           # 输入/输出峰值走带（与 SpectrumSource 同 FIFO 机制）
│   └── util/
│       ├── SpscFifo.h              # juce::AbstractFifo 单生产者单消费者封装
│       ├── SmoothedValue.h         # 线性/对数参数平滑辅助
│       ├── UserData.h              # 用户数据目录（settings / Presets）
│       ├── PresetBank.h/.cpp       # 17 个工厂预设（内嵌数据，含 3 个 Sideband + Growl 1/2/3 + Anger 1/2/3）
│       └── PresetIO.h/.cpp         # T42：自定义预设 *.nvpreset 导入/导出 + 用户库列表
└── tests/
    ├── CMakeLists.txt              # 两个 console target：unit_tests / offline_render
    ├── unit/                       # JUCE UnitTest 派生用例（60 类 / 332 用例，TESTPLAN §1）
    └── offline_render/             # RenderCore + CLI 渲染器 + smoke_test.py（TESTPLAN §2/§3）
```

约束：`source/` 只通过 JUCE 公开模块头文件使用框架；`tests/` 可访问 `source/dsp` 全部头文件（DSP 层不依赖插件壳，可独立测试）。

## 2. 模块职责

| 模块 | 职责 | 关键约束 |
|------|------|---------|
| `PluginProcessor` | 生命周期编排：prepareToPlay 预分配全部引擎；processBlock 串联信号链；状态保存/恢复 | 本身不做 DSP；零分配（prepare 后） |
| `ParameterLayout` | 由 SPEC §4 表机械生成 APVTS 参数 | ID 与 SPEC 逐字一致；改表必改 SPEC |
| `LoudnessCurve` | Band Gain 的归一化↔dB 非线性映射 + −48 dB 静音哨兵（T71） | 参数范围、旋钮文本与 BandProcessor 门控共用；0% 显示 −∞ |
| `ParamSnapshot` | 每块开头从 `getRawParameterValue`（atomic load）填充的 POD 快照 | 音频线程唯一参数来源 |
| `RandomModSource` | S&H 噪声 + one-pole 平滑 → 连续随机控制信号 | 独立 RNG 实例；prepare 预分配 |
| `ModLFO` | 4 波形 LFO（FullWave=`\|sin\|`）+ slew 限制 | 方波经 slew 防 click（R5） |
| `ModDelayLine` | 分数延迟线；输入调制信号（cents 域）→ 延迟深度（samples） | 容量按 SPEC §5.2；4 点插值 |
| `PitchModUnit` | Jitter（RandomModSource）+ Vibration（ModLFO）调制量相加，驱动一条 ModDelayLine | 单延迟线/频段（DECISIONS §1.3） |
| `ShimmerUnit` | `gain = 1 − depth·m01(t)`（单向模型，m01 ∈ [0,1]） | 独立 RandomModSource |
| `HarmonicExciter` | 2x oversampling → waveshaper(drive) → 带通(tone) → 下采 → 与干信号按比例混合（失真先于滤波，审查 O4 追认） | polyphase IIR half-band（DECISIONS §3.2）；drive=0 时整体旁路（R9）；**T35 评审结论：保留为 legacy 引擎**（`engine_version=0` 时使用，R18/R22） |
| `SidebandEngine` | HP(lowcut) → 可变分数延迟线（**T43 单抽头 PM / T58 恒包络相位雾**：单次 PM 读点 → wetGain → `PhaseFog`，无干净 tap、无 fog 延迟、无 shelf）→ **后置 HP(lowcut)**（T39 rev37d）→ **Bright 输出亮度级**（T40：1.5 kHz 高频倾斜 + 3.2 kHz 临场钟形，作用于 dry+wet，与 roughness 正交）；T58 的 Hilbert/相位控制由 `FogCore` 提供，pn 700–1400 Hz 仍为 sideband-only；dry 延迟匹配（R15 模式）；输入 f0/clarity/voiced 由 F0Tracker 喂入 | 每频段实例；depth=0 / enable=off 时湿路径零 CPU；rough=0 干净读点、相位雾关闭；Bright=0 时亮度级整体跳过；**固定基线 Lmax = A(100%) + F + 4**（F 仅 PDC 预留，T45）计入报告延迟，Sideband 引擎恒报 Lmax（与 depth/band/SB 开关无关）——SPEC §5.1，T38/T43/T45；Sideband Rough 只处理湿路径，dry 满电平（T51 scope 规则） |
| `FogCore`（T51/T58/T59/T60） | 两套 Rough 的唯一雾核：8 段全通 IIR Hilbert 正交对 + 恒包络相位旋转 `i·cos(phi) − q·sin(phi)`；`phi` 由 20–200 Hz 主控制（0.70）+ 500–3000 Hz 细节控制（0.30）相加，R100 4 rad RMS。控制 RNG 不进入音频；无延迟线、无附加噪声源/AM/底雾；不持有参数平滑 | Band Effects 与 Sideband 只差作用范围；Sideband pn/PM/voiced/wetGain/lowcut/Bright 不进核心；不改报告延迟；1 kHz R30/R50 中心线 ±5 86.11/32.02 %、±100 内 95.40/69.84 %；静音输出精确 0 |
| `F0Tracker` | MPM/NSDF F0 检测：环形缓冲 + hop 节律 NSDF（FFT 加速）+ 轨迹平滑 + voiced/hold/unvoiced 状态机 | **全局单实例**（MultibandEngine 持有，mono 下混输入）；Sideband 与 T66 全局 Growl 共用（R17/R54）；休眠条件 Legacy + Growl=0，或 Sideband 全 depth=0（零检测 CPU） |
| `GrowlEngine`（T66） | 全局湿路：20 ms 平滑 Amount/Rate/Tone + 共享每采样 AM 增益（N3：m1=.45、2×=.12、60–100 Hz 噪层 8%、7/5 Hz 随机 jitter/depth）+ clarity/voiced 门控 + 每声道双 biquad tone EQ（0 = −3 dB@4k Q1.2 + −1 dB@8k；Amount 缩放）；无延迟线 | 位于 Formant 之后、Dry/Wet 之前；Amount=0 逐位透明；不改变 PDC；L/R 共享控制、EQ 状态独立；无自动增益 |
| `CrossoverBank` | Butterworth Q 表级联 biquad 的 LR-N（LR2/4/6/8/10/12，T37 扩展至 6 档）；1–3 段；系数控制速率重算 + 交叉淡化 | **double 系数/状态（R7/T47）**；相邻段极性交替 |
| `BandFogUnit`（T49；UI 名 Rough，T50/T51/T58） | 整段 Rough（原 Fog）：频段链末端（引擎混合后）对 Band 输出做恒包络相位雾；无延迟线，相位控制来自 `FogCore`（与 Sideband Rough 完全一致） | Rough=0 逐位透明；20 ms 平滑；无延迟/无 PDC 变化（R21/R44/R50）；Band On=off 随整链旁路 |
| `BandProcessor` | 单频段信号链：PitchModUnit → ShimmerUnit →（engine_version ? SidebandEngine : HarmonicExciter，5 ms 交叉淡化）→ BandFogUnit → 每频段增益；Band On 5 ms 直通旁路（T38/T45） | depth=0 时各单元位透明，昂贵湿路径（oversampling/wet）跳过；Band On 关闭仍计算链路但输出 Lmax 对齐 dry；Band Effects 的 Rough 与引擎/Sideband 独立，仅作用范围为整段 |
| `MultibandEngine` | 分频 → 并行 3 频段 → 求和 → Formant → Growl → Dry/Wet → Trim；段数变化时启用/禁用频段与分频点；持有全局 F0Tracker 并广播 f0/clarity（T29/T66） | 1 段时分频器完全旁路；Growl 只作用于湿路，默认 0 逐位透明 |
| `FormantEngine`（T68） | 2048 点 causal STFT（hop 512，periodic Hann）→ cepstral 包络（0.5 ms 默认 + 1.5× 对比度）→ 150–5500 Hz 带限 `f/r` 搬移 → 原相位 → OLA；消费后清零 OLA 环；帧能量归一 | 参数帧边界平滑；shift=0 逐位透明；latency 2048；零分配（A7 已被 R56 取代） |
| `LatencyManager` | 跟踪 formant_enabled，报告延迟 = formant + 引擎固定延迟，执行切换交叉淡化 | 切换只在块边界发生；depth/band 自动化不改变报告值（T45） |
| `SpscFifo` / `SpectrumSource` | 音频线程写、UI 线程读的输入信号抽点通道 | 写满即丢（永不阻塞音频线程） |
| `NeoLookAndFeel` | 新拟物主题：DopaminePalette 色板门面、双向柔影（ShadowCache）、旋钮/开关/按钮/tab/plot 绘制；section/role 由组件属性沿父链解析 | 色值只在 `DopaminePalette.h`；几何与绑定不变（T64，R53） |
| `PresetBank` | 17 个工厂预设（内嵌数据，含 3 个 Sideband + Growl 1/2/3 + Anger 1/2/3，T35/T40/T61/T66/T67）+ APVTS 状态导入导出 | 预设仅经消息线程加载 |
| `PresetIO`（T42） | 自定义预设 `*.nvpreset`：APVTS 状态 ↔ UTF-8 XML 包装（formatVersion/stateVersion/name）、导出（强制 bypass/debug 关闭）、导入（wrapper 或裸 PARAMETERS）、用户库列表；`UserData` 提供 `<userAppData>/NeoVoiDrive/{Presets,settings}` | 仅消息线程；旧状态迁移复用 `PluginProcessor::applyStateXml`（R38） |

## 3. 数据流

```
                    ┌───────────────────────────── 音频线程（processBlock）────────────────────────────┐
                    │                                                                                │
 IN ──┬── tap ──► SpectrumSource(SPSC FIFO)      ParamSnapshot = load(APVTS atomic values)            │
     │                                                                                               │
     ▼                                                                                               │
 CrossoverBank（1–3 段, LR-N IIR）                                                                    │
     ├─► BandProcessor b1: PitchModUnit → ShimmerUnit → Engine → BandFog ──────────────────┐ │
     ├─► BandProcessor b2: 同上（独立 RNG/延迟线）                                                ├─┤ │
     ├─► BandProcessor b3: 同上（band_count<3 时禁用）                                            ┘ │ │
     │                                                                                          Σ 求和│
     │（band_count=1 时 CrossoverBank 旁路，单 BandProcessor 走全频）                                 │ │
     ▼                                                                                               │
 FormantEngine（formant_enabled 时启用；环形缓冲→STFT(hop=512, frame=2048)→包络搬移→OLA）              │
     ▼                                                                                               │
 Dry/Wet 混合 → Output Trim（dry/Bypass/Band off 全部延迟到报告值：Legacy 0/2048，Sideband Lmax/2048+Lmax） │
     ▼                                                                                               │
 OUT ──► Meter tap（峰值，SPSC FIFO → UI）                                                           │
                    └────────────────────────────────────────────────────────────────────────────────┘

 UI 线程：60 Hz CrossoverDisplay / 30 Hz BottomBar Timer → 读 Spectrum/Meter FIFO → 重绘
          用户操作 → Attachment → APVTS（原子值）→ 下一块被 ParamSnapshot 读取
```

频段内顺序依据：音高调制（延迟线）在最前，使 shimmer/exciter 作用于已变调信号；exciter 最后，避免其非线性产物被延迟线二次涂抹。

## 4. 类/模块关系

```
AudioProcessor
 └── PluginProcessor
      ├── AudioProcessorValueTreeState (params::ParameterLayout 构建)
      ├── params::ParamSnapshot            <── 每块填充（atomic loads）
      ├── dsp::MultibandEngine
      │    ├── dsp::F0Tracker             (全局单实例，T27/T29；mono 下混输入，广播 f0/clarity)
      │    ├── dsp::CrossoverBank          (LR-N, 1–3 段)
      │    └── dsp::BandProcessor ×3
      │         ├── dsp::PitchModUnit
      │         │    ├── dsp::RandomModSource   (jitter)
      │         │    ├── dsp::ModLFO            (vibration)
      │         │    └── dsp::ModDelayLine      (共享一条)
      │         ├── dsp::ShimmerUnit ── dsp::RandomModSource
      │         ├── dsp::HarmonicExciter ── juce::dsp::Oversampling (IIR half-band)   [legacy, engine_version=0]
      │         ├── dsp::SidebandEngine ── ModDelayLine / RandomModSource 复用        [sideband, engine_version=1, T28/T29]
      │         │    └── dsp::FogCore (Hilbert + phase fog)                         [T51/T58]
      │         └── dsp::BandFogUnit ── dsp::FogCore::PhaseFog                       [T49/T58, whole band]
      ├── dsp::FormantEngine
      │    └── FormantEngine（T68 自研 2048 点 STFT 包络搬移；无第三方运行时依赖）
      ├── dsp::GrowlEngine (AM + tone EQ; wet only, zero latency)          [T66, R54]
      ├── dsp::LatencyManager
      ├── ui::SpectrumSource  (SPSC FIFO, 写端)
      └── (meter tap FIFO, 写端)

AudioProcessorEditor
 └── PluginEditor (ui::NeoLookAndFeel 注入)
      └── Content  （T63：1200×720 设计画布，编辑器对其整体做 AffineTransform::scale）
           ├── ui::TopBarComponent        (全宽：Logo/Bypass/Preset/Settings + T41 UI Size 回调；T72 Settings 弹窗按 uiScale 缩放)
           ├── 左栏 812 px（70%）
           │    └── ui::CrossoverDisplay  (频谱 0…−72 dBFS + 包络 +18…−18 dB + 低透明度网格 + 分频点拖拽 + 段数/Slope + Gain) ← SpectrumSource 读端
           └── 右栏 348 px（30%）
                ├── juce::TabbedComponent → ui::BandCardComponent ×3 (band_count 联动灰显)
                │    └── 每个 TabBarButton 内、Band N 文字左侧：无文字 Band On 开关
                └── ui::BottomBarComponent (Growl/Master 两页签；Master：Formant/DryWet/Trim；仅 L/R 表；latency 移至 Settings) ← meter FIFO 读端
```

依赖方向：ui → dsp → （无环）；params 被两者引用；util 不依赖任何业务模块。

## 5. 第三方库集成方式

### 5.1 JUCE 9.0.0

- 本地存在 `stuff/JUCE` 时 `add_subdirectory(stuff/JUCE)`；否则 CMake 用 FetchContent 拉取 tag `9.0.0`（`GIT_SHALLOW`，固定版本）。`juce_add_plugin(NeoVoiDrive ... FORMATS VST3 Standalone)`（CMake API 见 <https://github.com/juce-framework/JUCE/blob/9.0.0/docs/CMake%20API.md>，R10）
- `target_compile_features(... cxx_std_20)`；MSVC `/std:c++20`（项目自身使用 `std::span` 等 C++20 特性）
- 链接模块见 SPEC §7；`juce_generate_juce_header` 可选启用
- 不修改任何 JUCE 检出目录；升级 JUCE 需同时更新 `CMakeLists.txt` 的 `GIT_TAG`

### 5.2 stftPitchShift-2.0（历史参考，不在仓库中）

- **不 add_subdirectory、不加入 include path、不编译/链接任何源码**。T12–T67 期间它只服务 FormantEngine；T68 用自研 STFT 包络搬移（R56）替换后，运行时完全不依赖该库。
- golden-audio 素材已独立为 `tests/assets/voice.wav`（`NVD_VOICE_WAV`），缺失时相关用例自动跳过。上游参考目录只存在于本地开发环境，未提交到仓库。
- 分发：**不随发行包分发**；`tests/assets/stftPitchShift-LICENSE.txt` 仅保留素材来源许可。

### 5.3 测试依赖

- 不引入 Google Test 等大型依赖；单元测试用 JUCE 自带 `juce_core::UnitTest` + `UnitTestRunner`（console target）
- 离线渲染器：console target，`juce_audio_formats` 读写 wav（测试代码允许 IO/分配，实时纪律不适用）

## 6. 线程模型

| 线程 | 允许 | 禁止 |
|------|------|------|
| **音频线程**（processBlock 及引擎 process） | 读 ParamSnapshot；预分配 buffer 的读写；SPSC FIFO 写（满即丢）；`ScopedNoDenormals` | 堆分配、锁、异常、阻塞、文件 IO、log/printf、`getRawParameterValue` 之外的 APVTS 交互 |
| **UI/消息线程** | Attachment 读写 APVTS；Timer 拉 FIFO 重绘；预设加载；打开 Settings | 直接触碰 DSP 引擎状态 |
| **宿主线程**（setStateInformation 等） | ValueTree/XML 恢复 → APVTS `replaceState` | 直接调用引擎 |

同步原语（仅此三种，全部无锁）：
1. **参数**：APVTS atomic 存储 → 每块开头填充 `ParamSnapshot`（plain POD 拷贝）
2. **频谱/电平**：`SpscFifo`（`juce::AbstractFifo`）单写单读
3. **开关类状态**（bypass、formant_enabled、band_count）：经 APVTS 参数表达，无额外共享变量

Formant 引擎重建（framesize/采样率变化）只发生在 `prepareToPlay` / `releaseResources`（非音频线程并发期，宿主保证调用序），运行中不重建。

## 7. 参数系统

1. **定义**：`params::ParameterIDs` 常量 + `ParameterLayout::create()`，机械对应 SPEC §4（**68 参数**；T46 移除 T39 TEMP debug，T49 追加 3 个 fog，T66 追加 3 个 Growl；当前 legacy 前缀 43：原 37 + T37 gain + T38 enable，sideband [43..60]、engine [61]、fog [62..64]、growl [65..67]；T37 后新增参数 `ParameterID(id, 2)`；C5/R19/T45/R44/R54）；任何参数变更必须先改 SPEC 再改代码
2. **持有**：`PluginProcessor` 内 `APVTS`（ctor 初始化列表注入 layout）
3. **读取**：`processBlock` 开头 `ParamSnapshot::fillFrom(apvts)`（每参数一次 atomic load）→ 引擎只读快照
4. **平滑**：SPEC §4"平滑"列在 DSP 单元内实现（`juce::SmoothedValue` 或一阶斜坡，控制信号域）；choice/bool 参数经交叉淡化切换（分频系数、formant 开关）
5. **持久化**：`getStateInformation` = APVTS `copyState()` → XML（含 `version` 属性；v1.1 起升级为 "2"，v1 → 强制 legacy 迁移分支见 T30）；`setStateInformation` 校验版本、容错缺失参数（未来新增参数取默认值）
6. **预设**：`PresetBank` 内嵌 **17** 个工厂预设（8 legacy + 3 Sideband + Growl 1/2/3 + Anger 1/2/3，T35/T40/T61/T66/T67）；工厂预设按**完整快照**加载（先恢复所有参数默认值，再应用预设值，避免 Bypass/频段/旧效果残留）；T69 起 Preset 菜单按 **Voice Drive**（Growl 1/2/3 + Anger 1/2/3）/ **Sound Effects**（其余 11 个）分组，分组由 `PresetBank::Category` 单源提供、菜单遍历两遍而底层索引不变；顶栏 Preset 菜单 = **Voice Drive / Sound Effects**（共 17）+ **User Presets ▸**（T42，扫描 `<userAppData>/NeoVoiDrive/Presets/*.nvpreset`）+ **Export Preset… / Import Preset… / Show Preset Folder**（消息线程文件对话框；导出/导入走 `util::PresetIO`，状态应用复用 `PluginProcessor::applyStateXml` 的 v1 迁移；`bypass` 在导出/导入数据中强制为 0）
7. **UI 绑定**：`SliderAttachment` / `ButtonAttachment` / `ComboBoxAttachment`；频段数由 `band_count` 驱动卡片标签页增减（T38）。T63 起每个 TabBarButton 的 extra component（Band N 文字左侧）绑定自己的 `{n}_enable`；页签重建时先释放 ButtonAttachment 再清空 tabs；Band 卡通过 APVTS listener（AsyncUpdater）刷新灰化。T62 起 Sideband UI 暂停（`{n}_sideband_*` 参数与 DSP 保留，经预设/自动化访问），Band 卡不再有 SB On 灰化联动；Settings 隐藏 Engine 下拉，`engine_version` 默认 Legacy，旧 v2 工程/自动化/Sideband 预设仍可显式选择 Sideband
8. **UI 缩放（T41/T72）**：主编辑器把 1200×720 设计画布整体 `AffineTransform::scale`；Settings 弹窗是独立顶层窗口，内部同样使用 340×496 设计画布 + transform，窗口尺寸 = `round(设计尺寸 × uiScale)`（100 % = 340×496，150 % = 510×744）。弹出菜单（Preset / ComboBox）也是独立窗口，由 `NeoLookAndFeel::setUiScale()` 缩放字体、边框、行高、勾选/箭头与自绘阴影；编辑器 `setUiScale()` 时同步更新当前 L&F，Settings 弹窗使用自己的 popup L&F。

---

*版本：v1.38（2026-11-07 文档整理：根目录只保留面向用户的 `README.md`，`AGENTS.md` 与开发 README 移入 `docs/`；依赖口径清理：golden 素材移至 `tests/assets/voice.wav`；构建/测试/运行均不引用 stftPitchShift，目录可整体删除）；v1.37（2026-11-05 T74：FogCore/Sideband/Growl 的每样本 sin/cos/tanh 改用 `TrigApprox.h`（JUCE Pade，相位归一到 ±π、tanh 钳 ±5）；A/B 无可闻差异，P01 formant ON 3.93→2.76–2.95 %；332 用例/60 类）；v1.36（2026-11-03 T73：Vendor/Version→ M.R.V.D. / 0.0.1（About `alpha 0.0.1`）；右栏 65/35 → 70/30（左 812 / 右 348）；Band 页签文字在扣除开关后的文本区内居中；329 用例/60 类）；v1.35（2026-11-03 T72：Settings 弹窗与弹出菜单随 uiScale 缩放（字体/边框/行高/自绘阴影），Settings 设计画布 340×496；329 用例/60 类）；v1.34（2026-11-03 审查 P3 追加：tone 系数控制速率、参数名去重、参数 NaN 归零；327 用例/60 类）；v1.33（2026-11-03 审查 P3 清理：状态防御、首块 snap、a11y/手势、构建宏 About、tail 0.05 s、页签保留；325 用例/60 类）；v1.32（2026-11-03 审查 P1/P2 修复：Band 路由 5 ms 淡出/切路由/淡入 + 隐藏链状态清理；CrossoverBank slope 交叉淡化；FormantEngine 移除 AVX2；频谱读取最新块；F0 hold 绝对计时；Growl 空闲跳过；322 用例/60 类）；v1.31（2026-11-02 T71：Band Gain 非线性响度曲线（LoudnessCurve；50%=0 dB、100%=+12 dB、0%=−∞/−48 哨兵；衰减 0.08d+0.92d³、提升 0.80d+0.20d³）；311 用例/60 类）；v1.30（2026-11-01 T70：CrossoverDisplay 双纵轴（频谱 0…−72 dBFS / 包络 +18…−18 dB）+ 低透明度网格；307 用例/58 类）；v1.29（2026-11-01 T69：Preset 菜单按 Voice Drive / Sound Effects 分组（PresetBank::Category 单源），底层索引与加载结果不变；306 用例/58 类）；v1.28（2026-11-01 T68：FormantEngine 改为自研 2048 点 STFT 包络搬移（hop 512、0.5 ms cepstral lifter + 1.5× 对比度、150–5500 Hz 带限、原相位、帧能量归一、OLA 消费清零）；移除 stftPitchShift 的 include/编译依赖（仅保留 voice.wav 测试素材）；`formant_quefrency` 默认 0.5 ms；参数/UI/PDC 不变；305 用例/58 类；P01 formant-only 3.01%；A06 46.51 dB）；v1.27（2026-11-01 T67：工厂预设 13 → **17**，新增 Growl 1/2/3 与 Anger 1/2/3 两组轻/中/重预设；无 DSP/参数/UI/PDC 变化）；v1.26（2026-10-31 T66：新增 GrowlEngine（全局湿路 AM + tone EQ、F0 复用、零 PDC）；Master 卡改为 Growl/Master 嵌套页签（Growl 默认）、Formant 居中、仅 L/R 表、latency 移至 Settings；68 参数/13 预设；58 类/302 用例）；v1.25（2026-10-31 T65/T64：新增 `DopaminePalette.h`（唯一色源）与新拟物主题；ShadowCache 增 light/dx/section；T65 修复 Standalone 缩放裁切、Band 3 改明黄；54 类/286 用例；v1.24（2026-10-30 T63：画布 **1200×720**（默认 **1020×612**）；左 754（65%）/右 406；每个页签内部、Band N 文字左侧放无文字 Band On 开关（带边距、独立绑定），BandCard 移除头部标题行；右栏 tabs 430 + Master 188；54 类/284 用例；v1.23（2026-10-30 T62：editor 改为 Header 全宽 + 左 600 频响 / 右 400 Band+Master 的两栏树；BandCard 移除 Sideband UI；默认 engine Legacy、Settings 隐藏 Engine；54 类/283 用例；v1.22（2026-10-30 T61：新增 Growl 1 工厂预设（Legacy，12 预设），无 DSP/参数/UI/PDC 变化；v1.21（2026-10-27 T60：FogCore 相位控制主/细节权重 0.60/0.40 → 0.70/0.30（收回 T59 远侧带雾层）；1 kHz R30/R50 中心线 ±5 86.11/32.02 %、±100 内 95.40/69.84 %；无参数/延迟/PDC 变化；53 类/280 用例；v1.20（2026-10-26 T59：FogCore 相位控制主/细节权重 0.75/0.25 → 0.60/0.40（细节优先，细化 R30 颗粒）；1 kHz R30/R50 中心线 ±5 86.27/32.44 %、±100 内 93.92/62.34 %；无参数/延迟/PDC 变化；53 类/280 用例；v1.19（2026-10-26 T58：FogCore 改为 Hilbert 恒包络相位雾（20–200/500–3000 Hz，R100 4 rad RMS），移除延迟噪声雾/AM；BandFog 无延迟线；1 kHz R50 中心线 ±5 31.90 %、±100 72.78 %，100 Hz 40.4 %/85.2 %；53 类/280 用例；v1.18：2026-10-26 T55：双频带 AM（已由 T58 取代）；v1.16：2026-10-26 T53：单抽头全湿、无干净 tap/无 shelf、fogDepth=amount^1.5；验收 R30–50；v1.15：T52：双抽头 + shelf（已由 T53 取代）；v1.14：2026-10-23 T51：新增 FogCore，BandFogUnit 与 SidebandEngine 共用同一雾核，grain 曲线/混合律统一，HF residual 0.15 → 0.30；Sideband dry 保持满电平；53 类/277 用例；v1.13：T50：Band Effects 的整段 Rough 显示名 Fog → Rough（参数 ID 不变），Sideband Rough 移至该行最右；v1.12：T49：新增 BandFogUnit / `{n}_fog` 整段雾化（65 参数）与 Band Effects UI 小节；v1.11：T48：采样率策略（信任宿主 + CLI 限制）；v1.10：T47：biquad double、Formant float 口径；v1.9：T46：TEMP debug 移除，62 参数；v1.8：T45：固定 Lmax + dry/Bypass/Band off 对齐 + 初始报告；测试 266 用例；v1.7：T44：雾摆幅 F 22→34、fogDrive 1.5 + C1 软限幅；v1.6：T43 SidebandEngine 双读点 + 深度无关雾 + 雾倾斜，L = A + F + 4 与报告口径；测试 264 用例；v1.5：T42 PresetIO 自定义预设导入/导出 + UserData；v1.4：editor 缩放画布 Content + Settings UI Size（75–150 %，默认 100 % = 654×799）；v1.3：T40 SidebandEngine 增 Bright 输出亮度级、63 参数、11 预设；详见 IMPACT_ANALYSIS.md / REFACTOR_PLAN.md / test_report.md T36–T58）*
