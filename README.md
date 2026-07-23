# VoiceFX KDver — 獨立可編譯的 NVIDIA VoiceFX（VST3）

這是 Xaymar 的 [TonPlugIns / VoiceFX](https://github.com/Xaymar/TonPlugIns) 的 fork。
原版要靠作者私有的 TonPlugIns 建置框架才能編譯；這個 fork 多做了一個
**自給自足、不依賴私有框架**的獨立 VST3 版本，叫 **VoiceFX KDver**，只用得到：

- Steinberg VST3 SDK
- NVIDIA Audio Effects (AFX) SDK（執行時動態載入，不打包進外掛）
- 內附的 Secret Rabbit Code 重取樣器
- `standalone/compat/` 裡幾個小墊片

> ⬇️ **直接下載**：到 [Releases](https://github.com/xup61069/TonPlugIns-VoiceFX/releases/latest)
> 抓最新版的 `VoiceFX-KDver-*-win-x64.zip`（Windows x64）。

## 功能（v2.1.0）
- **Mode**
  - **Noise** — 降噪（denoiser）
  - **Reverb** — 去殘響（dereverb）
  - **Both** — 降噪 + 去殘響
  - **Echo Cancel** — 聲學回音消除（AEC，見下）
- **Level** — 強度 0–100%（AEC 模式不作用）
- **Super Res** — 超解析度，疊在 Noise / Reverb / Both 上（AEC 模式不作用）
- 超緊湊的灰底 VSTGUI 介面

（原版的 Studio Voice / Speaker Focus 已移除：它們需要 Windows 拿不到的
NVIDIA AFX 2.x 模型，本來就跑不起來。）

## Echo Cancel（AEC）怎麼用
AEC 要一個「參考訊號」才能把喇叭／對方的聲音從麥克風裡消掉。這個版本用
**立體聲配對**餵參考訊號：

- **左聲道 = 麥克風**
- **右聲道 = 參考訊號**（喇叭／對方／系統播放的聲音）

在宿主裡把麥克風接到左、要消掉的聲音接到右（同一條立體聲輸入），Mode 選
**Echo Cancel**，輸出就是消完回音的乾淨人聲（複製到左右兩邊）。細節見
[`standalone/README.md`](standalone/README.md)。

## 系統需求
- Windows 10/11 x64、NVIDIA RTX GPU
- 已安裝 **NVIDIA Audio Effects SDK 1.6.1.2**（提供 `NVAudioEffects.dll` 與模型檔）
- 外掛在執行時動態載入上述 NVIDIA 執行檔，**本體不含** NVIDIA 的 DLL 或模型

## 安裝
1. 解壓縮，把 `VoiceFX KDver.vst3` 資料夾放到宿主會掃描的 VST3 目錄，例如
   `C:\Program Files\Common Files\VST3\`
2. 在宿主（例如 Element）重新掃描外掛

> 這是自用 fork（KDver）。與付費原版請放在不同資料夾，避免覆蓋你的授權版本。

## 自己編譯
完整的建置步驟、相依套件、以及設計說明都在
**[`standalone/README.md`](standalone/README.md)**。最精簡的版本：

```bat
cmake -S standalone -B standalone/build -G "Visual Studio 17 2022" -A x64 -DVST3_SDK_DIR=C:/vst3sdk
cmake --build standalone/build --config Release --target VoiceFX
```
產物：`standalone/build/VST3/Release/VoiceFX KDver.vst3`

## 版面
| 路徑 | 內容 |
| --- | --- |
| `standalone/` | **自給自足的 VST3 版本（本 fork 的重點）** — 見其 README |
| `source/` | 原版 TonPlugIns/VoiceFX 的原始碼（需私有框架才能建置） |
| `tools/afx-smoketest/` | 不需框架的 NVIDIA AFX 冒煙測試，用來在真機驗證 SDK 路徑 |
| `third-party/` | 內附的 NVIDIA AFX 標頭／匯入庫與重取樣器 |
| `NVIDIA-SDK-UPDATE.md` | 這個 fork 接了哪些 AFX 功能的說明 |

## 授權與致謝
原始 VoiceFX 由 **Michael Fabian 'Xaymar' Dirks** 撰寫，採 2 條款 BSD 式授權，
見 [`LICENSE.md`](LICENSE.md)。本 fork 沿用同一授權，並感謝原作者。

NVIDIA、Maxine、Audio Effects SDK 為 NVIDIA Corporation 之商標／產品；本專案
不重新散布 NVIDIA 的二進位檔或模型。
