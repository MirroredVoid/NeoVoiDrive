# NeoVoiDrive

> 三频段人声质感效果器 · VST3 · Windows 10/11 x64 · alpha 0.0.1

NeoVoiDrive 不改变人声的音高与时长，而是在 1–3 个可自由划分的频段上叠加细微扰动、调制与边带纹理，用于贴唱、旁白、有声书、播客、配音与角色音色设计。

它的典型用途是给人声增加“活人感”、粗糙度、嘶吼感或复古质感；不适合音高纠正、和声生成或复音乐器。

---

## 功能一览

| 模块 | 作用 |
|------|------|
| 三频段分频 | 1–3 段自由分频，Linkwitz-Riley 12–72 dB/oct |
| Jitter | 快速随机音高扰动 |
| Shimmer | 快速随机振幅扰动 |
| Vibration | 规则音高调制（Sine / Square / FullWave / Noise） |
| Rough | 整段相位雾化，把谐波中心线展成宽峰（30–50 用于“展宽但谐波仍在”） |
| Growl | 全局 F0 跟踪幅度调制 + 去刺 EQ，用于嘶吼/怒音 |
| Formant Shift | STFT 频谱包络搬移，改变“年龄/体型”音色，不改变音高与时长 |
| Master | Dry/Wet、Output Trim、L/R 输出电平表 |
| 预设 | 17 个工厂预设 + 用户 `.nvpreset` 预设库 |

---

## 安装

发行包是一个 ZIP，不需要安装器：

```
NeoVoiDrive-0.0.1-win64.zip
├─ VST3/NeoVoiDrive.vst3
└─ licenses/
```

**安装 VST3**

1. 解压 ZIP。
2. 将整个 `VST3\NeoVoiDrive.vst3` 文件夹复制到：
   `C:\Program Files\Common Files\VST3\`
   或你的 DAW 指定的 VST3 目录。
3. 在 DAW 中重新扫描插件，然后在人声轨上插入 NeoVoiDrive。

> 发行包仅包含 VST3。Standalone 仍是本地开发/调试构建，但不随发行包分发。

**系统要求**

- Windows 10 / 11 x64
- 支持 VST3 的宿主（Reaper、FL Studio 等）
- 常见采样率 44.1–192 kHz；插件按宿主采样率运行

**从源码构建**：见 [`docs/README.md`](docs/README.md)（CMake 3.22+ / Visual Studio 2022；首次配置会自动下载 JUCE 9.0.0）。

---

## 快速上手

1. **加载插件**：把 NeoVoiDrive 插到人声轨。
2. **先确认干声**：默认是 1 个频段、Legacy 引擎、所有效果深度为 0，插件完全透明且不报告额外延迟。播放人声，确认输入正常。
3. **选一个预设**：点顶栏 **Preset**。想要细腻质感选 **Sound Effects**；想要嘶吼/怒音选 **Voice Drive** 中的 **Growl** 或 **Anger**。
4. **按需加效果**：在右栏 Band 卡片中调 Jitter / Shimmer / Vibration，以及 **Rough**。需要多段处理时，在左栏把 **Bands** 切到 2 或 3，并调整 Xover 分频点。
5. **控制整体**：用 Master 页的 **Dry / Wet** 控制效果比例，用 **Output Trim** 补偿响度。
6. **A/B 对比**：点顶栏 **Bypass** 旁通；旁通带约 5 ms 等增益斜坡，不会产生 click。

---

## 界面速览

### 顶栏

| 控件 | 用途 |
|------|------|
| **Bypass** | 全局旁通；旁通不改变报告延迟 |
| **Preset** | 工厂预设（Voice Drive / Sound Effects）+ 用户预设 + 导入/导出 |
| **Settings** | UI Size、Formant Quefrency、延迟监视、About |

### 左栏：频谱与分频

- **频谱窗**：显示 In / Out / Both 频谱（默认 In），60 fps 刷新；可直接拖动 Xover 1 / Xover 2 分频线。
- **Xover 1 / Xover 2**：两个分频点，默认 200 Hz / 4 kHz。
- **Bands**：1 / 2 / 3 段。
- **Slope**：12–72 dB/oct；干净人声建议默认 **24 dB/oct（LR4）**。
- **Band 1–3 Gain**：每频段响度。旋钮 **50% = 0 dB**，100% = +12 dB，**0% = 静音（−∞ dB）**。

### 右栏：Band 卡片

- 每个频段一个页签，页签内、Band N 文字左侧是 **Band On** 开关；关闭时该段直通，卡片控件变灰。
- **Band Effects**：Jit Depth / Jit Rate / **Rough** / Shim Depth / Shim Rate。
- **Vibration**：Wave（Sine / Square / FullWave / Noise）、Depth、Rate。

### 右栏底部：Growl / Master

- **Growl 页**：Amount、Rate、Tone。Amount = 0 时透明；Rate 是相对跟踪 F0 的倍率；Tone 负值更亮、正值更暗。
- **Master 页**：Formant Shift、Formant On、Dry / Wet、Output Trim。
- **L/R 电平表**：只显示输出峰值。

### Settings

- **Quefrency**：Formant 包络平滑度，0.5–5.0 ms，默认 0.5 ms。
- **FFT Frame Size**：固定 2048，只读。
- **Monitor / Latency**：当前报告延迟（samples + ms）。
- **UI Size**：75%–150%，默认 100%（1020×612）；设置按用户保存，Settings 与弹出菜单同步缩放。
- **About**：版本信息。

---

## 常用调法

| 目标 | 建议 |
|------|------|
| 增加“活人感”、细微抖动 | Jitter Depth 100–300 cents、Jit Rate 100–300 Hz；Shimmer Depth 20–40% |
| 粗糙、撕裂感 | 提高 Jitter / Shimmer，或直接用 **Growl 1/2/3**（Legacy 频段调制） |
| 嘶吼、怒音 | Master → **Growl** 页：Amount 60–100、Rate 0.20 左右；或载入 **Anger 1/2/3** |
| 改变年龄 / 体型 | Formant Shift ±2–6 st，小幅调整；0 st 时逐位透明 |
| 加宽 / 雾化但保留谐波 | Rough **30–50**；Rough 100 是设计上的全雾终点，不建议用它做音色验收 |
| 金属芯 / 更明亮 | 载入 **Steel Core**（Sideband 引擎）；或通过 DAW 自动化调 `b2_sideband_bright` |
| 让低频不参与效果 | 提高 Xover 1、关闭对应 Band，或在 Sideband 参数中提高 Low Cut |

**Growl 与 Anger 的区别**：`Growl 1/2/3` 是 Legacy 频段调制（不使用全局 Growl）；`Anger 1/2/3` 在 Legacy 基座上叠加全局 Growl。编号越大越重。

**Rough 的听感**：它把谐波中心线展成宽峰，但保留音高锚；Rough 30–50 是“展宽但谐波仍在”的常用区间，Rough 100 接近全雾。接近 0 dBFS 的素材建议留 3–6 dB headroom。

---

## 预设

工厂预设共 **17 个**，在 Preset 菜单中按两组显示：

- **Voice Drive**
  - **Growl 1/2/3**：Legacy 频段调制，由轻到重
  - **Anger 1/2/3**：Legacy 基座 + 全局 Growl，由轻到重
- **Sound Effects**
  - Clean Voice、Warm Voice、Radio Voice、Lo-Fi Tape、Pitch Up、Pitch Down、Bright Exciter、Vintage Vib、Rough Air、Sideband Grit、Steel Core

其中 `Rough Air`、`Sideband Grit`、`Steel Core` 会显式使用 Sideband 引擎。Legacy 是默认引擎；Sideband 的旋钮目前不在 Band 卡片中显示，但参数、DSP 与预设完整保留，可通过预设、旧工程或 DAW 自动化访问。

**用户预设**

- **Export Preset…**：把当前全部参数保存为 `.nvpreset` 文件。
- **Import Preset…**：导入 `.nvpreset`（也接受 DAW 导出的状态 `.xml`）。
- **Show Preset Folder**：打开 `%APPDATA%\NeoVoiDrive\Presets\`；该目录下的 `.nvpreset` 会出现在 **User Presets** 子菜单中。

工厂预设按完整快照加载，不会残留上一个预设的 Bypass 或效果深度。UI Size 是用户级设置，不随预设保存。

---

## 延迟与 PDC

插件会向宿主报告延迟，宿主据此做自动补偿：

| 状态 | 报告延迟 |
|------|---------|
| Legacy，Formant 关 | **0 samples** |
| Legacy，Formant 开 | **2048 samples** |
| Sideband，Formant 关 | **Lmax**（48 kHz 为 53 samples，约 1.10 ms） |
| Sideband，Formant 开 | **2048 + Lmax** |

- **Growl 不增加延迟**，也不改变报告值。
- **Bypass 不改变报告延迟**，避免宿主反复重补偿。
- 默认实例是 Legacy，因此新工程通常是 0 samples；只有 Formant 或显式 Sideband 引擎才会增加延迟。
- 播放中切换 Formant 会引起一次 PDC 重补偿，现场演出中不建议频繁切换。

---

## 常见问题

| 问题 | 说明 |
|------|------|
| 打开插件后听不出效果 | 默认所有深度为 0；先载入预设，或提高 Jitter / Shimmer / Vib Depth / Rough |
| Sideband 控件在哪里 | 当前版本暂时隐藏；载入 3 个 Sideband 预设，或通过 DAW 自动化访问 `b{n}_sideband_*` |
| 打开 Formant 后延迟变大 | Formant 固定报告 2048 samples（Sideband 引擎再加 Lmax），由宿主 PDC 补偿，属预期 |
| 旧工程里 Band Gain 自动化变了 | 工程保存的是实际 dB，声音不变；但 DAW 中的归一化自动化会按新响度曲线重新解释，建议重新录制（详见用户手册 §7） |
| 低频段拉了效果却没声音 | 检查该段 Band On、Xover 范围，以及 Sideband Low Cut 是否高于该段内容 |
| 支持哪些格式/平台 | Windows x64 的 VST3（发行包）；Standalone 仅用于本地开发调试，不随包分发；不支持 AU / AAX / CLAP、macOS / Linux、环绕声 |

---

## 更多文档

| 文档 | 内容 |
|------|------|
| [`docs/USER_MANUAL.md`](docs/USER_MANUAL.md) | 完整用户手册：界面、全部参数、延迟、预设与限制 |
| [`docs/README.md`](docs/README.md) | 开发/构建/测试说明 |
| [`docs/AGENTS.md`](docs/AGENTS.md) | 项目协作约定与架构摘要 |
| [`docs/SPEC.md`](docs/SPEC.md) | 产品需求规格与参数定义 |
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | 架构、目录结构与模块职责 |
| [`docs/THIRD_PARTY_LICENSES.md`](docs/THIRD_PARTY_LICENSES.md) | 第三方许可、字体许可与发行包内容 |

---

## 许可

NeoVoiDrive 本体以 **GNU Affero General Public License v3.0（AGPLv3）** 发布，许可全文见仓库根目录 [`LICENSE`](LICENSE)。

- **Google Sans Code**：SIL Open Font License 1.1，随安装包附带许可全文（[`LICENSES/GoogleSansCode-OFL.txt`](LICENSES/GoogleSansCode-OFL.txt)）。
- **JUCE 9.0.0**：AGPLv3 或 JUCE 商业许可；本项目按 AGPLv3 使用 JUCE，闭源分发须另行持有有效的 JUCE 商业许可。

完整说明见 [`docs/THIRD_PARTY_LICENSES.md`](docs/THIRD_PARTY_LICENSES.md)。
