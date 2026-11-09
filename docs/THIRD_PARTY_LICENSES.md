# 第三方许可与发布合规（2026-11-08）

> 适用范围：NeoVoiDrive 的二进制发行包（仅 VST3）。
> 打包命令见 `CMakeLists.txt` 末尾（`cpack -C Release`）；`licenses/` 目录随包分发。
> 项目本体以 **AGPLv3** 发布，全文见仓库根目录 `LICENSE`；本文件同时记录第三方组件与发行包合规。

## 1. Google Sans Code（嵌入字体）

| 项 | 内容 |
|---|---|
| 文件 | `assets/fonts/GoogleSansCode-VariableFont_MONO,wght.ttf`（经 `juce_add_binary_data` 嵌入二进制） |
| 版权 | Copyright 2025 The Google Sans Code Project Authors (github.com/googlefonts/googlesans-code) |
| 许可 | SIL Open Font License 1.1 |
| 许可全文 | [`LICENSES/GoogleSansCode-OFL.txt`](../LICENSES/GoogleSansCode-OFL.txt)，并随安装包放在 `licenses/` |

OFL 允许随软件嵌入、捆绑再分发与商业使用；禁止单独出售字体文件。修改后的字体若再分发，须继续以 OFL 1.1 授权。

## 2. JUCE 9.0.0

| 项 | 内容 |
|---|---|
| 位置 | 本地 `stuff/JUCE`（只读）或 CMake FetchContent 拉取的 tag `9.0.0` |
| 许可 | 双许可：AGPLv3 或 JUCE 商业许可（<https://juce.com/legal/juce-9-licence/>） |
| 发行要求 | 本项目按 **AGPLv3** 使用 JUCE，需同时提供完整对应源码并遵守 AGPL 条款；**闭源分发必须另行持有有效的 JUCE 商业许可** |

JUCE 源码不随二进制包分发；仓库通过固定 tag `9.0.0` 获取同一版本，源码对应关系可复现。

## 3. 开发测试素材 voice.wav（不构成依赖）

| 项 | 内容 |
|---|---|
| 文件 | `tests/assets/voice.wav`（golden 音频测试 A05/A06 的嗓音素材） |
| 来源 | 复制自 stftPitchShift-2.0 的 `examples/voice.wav`（MIT，Copyright (c) 2022 Juergen Hock） |
| 许可 | MIT；全文见 `tests/assets/stftPitchShift-LICENSE.txt`（随素材保留，不随发行包分发） |
| 当前用途 | **仅开发/测试**：插件的构建、运行与发行包均不引用该文件；stftPitchShift 源码不参与编译/链接 |
| 发行要求 | 不随二进制包分发；该素材位于 `tests/assets/`，上游 stftPitchShift 源码不在本仓库 |

## 4. 开发期参考音频（不纳入仓库）

`stuff/` 下的 `0.mp3`、`C2*.mp3/wav`、`Q1.mp3`、`R.mp3`、`test_*.wav` 等仅用于本地开发期听感/分析，**既不在仓库中，也不随发行包分发**。若将来要公开或用于演示，须先确认其版权与授权。

## 5. 字体许可与 EULA 结论

`LICENSES/GoogleSansCode-OFL.txt` 是完整的 **SIL OFL 1.1 字体许可**。Google Sans Code **没有额外的字体 EULA**；OFL 本身就是它的授权条款，不能也不需要用 EULA 替代。

- OFL 明确允许字体随软件**嵌入、捆绑、再分发并随软件销售**；条件 2 要求每份字体副本附带版权声明与许可全文，已由发行 ZIP 的 `licenses/GoogleSansCode-OFL.txt` 满足。
- 字体 name 表没有 Reserved Font Name；项目未修改、未重命名字体文件，只按原样嵌入二进制。
- 字体元数据声明 Google / Google Sans / Google Sans Code 是 Google LLC 的商标。OFL 不授予商标权；本项目仅用该字体渲染 UI 文本，不以 Google 名称或字体名称做产品命名/宣传，也不暗示 Google 背书。
- **项目本体按 AGPLv3 发布**（`LICENSE`），不另附专有 EULA。OFL 只约束字体；字体许可与项目许可证相互独立。
- 当前发行单位是完整 ZIP，许可文本随包分发。若未来改为单独分发 VST3 二进制（不随 ZIP 附带 `licenses/`），应再把 OFL 文本复制到 VST3 的 `Contents/Resources/`，确保“每份副本附带许可”仍然成立。

> 以上为对授权文本与官方 FAQ 的事实梳理，不构成法律意见；最终发布条款由产品方/法务确认。

## 6. 打包内容

`cpack -C Release` 生成的 ZIP 结构：

```
NeoVoiDrive-0.0.1-win64/
  VST3/NeoVoiDrive.vst3/
  licenses/GoogleSansCode-OFL.txt
  licenses/THIRD_PARTY_LICENSES.md
```

不含 Standalone、源码、`stuff/`、`assets/`、`tests/assets/`、示例音频或设计文档。
