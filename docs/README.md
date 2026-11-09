# NeoVoiDrive 开发说明

> 面向开发/构建/测试；插件安装与使用请看根目录 `README.md` 与 `docs/USER_MANUAL.md`。

三频段人声效果器(VST3 发行 / Standalone 本地调试),JUCE 9.0.0,C++20,Windows x64。界面为左右分栏（Header 全宽 + 左 70% 频响/分频 + 右 30% Band 卡片/Master；每个页签内、Band N 文字左侧有无文字 Band On 开关）；默认 100% = 1020×612。视觉为**新拟物 + 多巴胺色系**（T64：同色族表面、双向柔影、语义分色；色板唯一来源 `source/ui/DopaminePalette.h`）。默认引擎 Legacy；Master 卡默认显示 Growl 页（T66，全局 F0 AM + 去刺 EQ），Sideband 的 UI 暂时隐藏但参数/DSP/预设保留。

核心 DSP:频段独立 Jitter / Vibrato / Shimmer / 整段 Rough / Harmonic Exciter,Linkwitz-Riley 分频合流,专用 STFT 包络搬移 Formant Shift(T68,自研,无第三方运行时依赖)。

## 前置要求

- Windows 10 / 11 x64
- Visual Studio 2022（Desktop development with C++ 工作负载）
- CMake ≥ 3.22（可用 VS 自带版本）
- Git 与 github.com 访问（首次配置需要下载 JUCE 9.0.0）
- Python 3（可选；存在时启用 `offline_smoke` 测试）

## 构建

仓库不包含 `stuff/`。若本地存在 `stuff/JUCE` 会直接使用；否则 CMake 在首次配置时通过 FetchContent 拉取 JUCE **9.0.0**（固定 tag）。网络受限时可指定本地副本：`-DFETCHCONTENT_SOURCE_DIR_JUCE=<JUCE 源码目录>`。

```bash
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

产物:

- VST3:`build/NeoVoiDrive_artefacts/Release/VST3/NeoVoiDrive.vst3`
- Standalone:`build/NeoVoiDrive_artefacts/Release/Standalone/NeoVoiDrive.exe`（本地调试，不随发行包分发）

### 发布包

```bash
cpack -C Release --config build/CPackConfig.cmake -B build/package
```

生成 `build/package/NeoVoiDrive-<version>-win64.zip`(VST3 bundle + `licenses/`);只含构建产物,不含 Standalone、源码与测试素材。许可状态见 `docs/THIRD_PARTY_LICENSES.md`。

### 可选构建开关

| 开关 | 默认 | 说明 |
|------|------|------|
| `NVD_ENABLE_LTO` | OFF | 发布打包建议开启(MSVC `/GL` + `/LTCG`,构建时间显著增加) |

```bash
cmake -B build -G "Visual Studio 17 2022" -A x64 -DNVD_ENABLE_LTO=ON
```

### 指令集

- 全工程 SSE2 基线(兼容 2013 年前 CPU;含 `source/dsp/FormantEngine.cpp`,无 AVX2 编译单元)

## 测试

```bash
cmake --build build --config Release --parallel
./build/tests/unit_tests_artefacts/Release/unit_tests.exe    # 332 组单测（60 类）
ctest --test-dir build -C Release                             # unit_tests + offline_smoke
```

性能门限可用环境变量放宽(慢速 CI):

- `NVD_P01_GATE_ON` / `NVD_P01_GATE_OFF`(P01 CPU 基准,默认 15.0 / 6.0)
- `NVD_P04_GATE`(P04 多实例线性门限,默认 9.6)

金标准音频测试(A05/A06)需要素材 `tests/assets/voice.wav`,经宏 `NVD_VOICE_WAV` 指向;缺失时自动跳过。

## 离线渲染工具

```bash
cmake --build build --config Release --parallel --target offline_render
./build/tests/offline_render_artefacts/Release/offline_render.exe \
    --in=input.wav --out=output.wav [--sr=48000] [--block=512] \
    [--bits=16|24|32] [--seed=N] [--param b1_jitter_depth=50] ...
```

## 目录导读

```
assets/fonts/              内嵌字体 Google Sans Code(OFL 1.1,经 juce_add_binary_data 嵌入)
source/
  PluginProcessor.cpp/.h   音频处理器(状态管理、RT 链装配、延迟报告)
  PluginEditor.cpp/.h      UI 两栏总装(Header / 左频响 / 右 Band+Master)
  params/                  参数布局(68 参数 APVTS)与 RT 快照；LoudnessCurve(T71 Band Gain 非线性响度:50%=0 dB、0%=−∞)
  dsp/                     DSP:MultibandEngine、Crossover、Band 单元、FormantEngine、LatencyManager
  ui/                      控件:TopBar、CrossoverDisplay、BandCard、BottomBar、LookAndFeel、频谱源
  util/                    PresetBank(17 个工厂预设,菜单按 Voice Drive / Sound Effects 分组)、PresetIO(自定义 .nvpreset 导入/导出)、UserData
tests/
  assets/                  golden 音频测试素材(voice.wav + 来源许可,不随发行包分发)
  unit/                    332 组单元测试(含 UI 离屏截图、布局、延迟实测、金标准、性能审计)
  offline_render/          RenderCore + CLI(wav→处理→wav)
samples/                   试听示例(Sample-before.wav / Sample-after.wav)
docs/                      随仓库发布的开发/设计文档(见下表)
```

## 文档

| 文档 | 内容 |
|------|------|
| `docs/USER_MANUAL.md` | 用户手册(参数速览、延迟与 PDC、预设与已知限制) |
| `docs/SPEC.md` | 产品需求规格(§4 参数、§5 延迟/缓冲预算) |
| `docs/ARCHITECTURE.md` | 架构总览、目录结构与模块职责 |
| `docs/AGENTS.md` | 协作约定与架构摘要 |
| `docs/THIRD_PARTY_LICENSES.md` | 第三方许可、发布合规与安装包内容 |
| `docs/README.md` | 本文件:开发/构建/测试说明 |

> 根目录仅保留面向用户的 `README.md`;本文件与 `docs/AGENTS.md` 位于 `docs/`。设计决策、测试计划、任务卡、审查记录与每轮测试报告属于本地开发记录，未随仓库分发。
