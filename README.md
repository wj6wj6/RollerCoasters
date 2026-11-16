# RollerCoasters

電腦圖學課程的 Windows 桌面過山車編輯器，使用 C++、FLTK 1.3.5 與 OpenGL。

## 建置與啟動

安裝 Visual Studio 2022 的 **Desktop development with C++**（MSVC v143、Windows SDK），以及 PowerShell 7.4 以上版本。
以下命令均在此 repository 根目錄執行：

```powershell
pwsh -File scripts/setup-tools.ps1
pwsh -File scripts/check.ps1 -Stage build -Config Debug
./out/build/windows/Debug/RollerCoasters.exe
```

目前支援 Windows x64 的 MSVC 與 clang-cl。隨附的 `.lib` 不支援直接切換 Linux、MinGW 或 ARM。
工具版本與官方下載的 SHA256 固定於 `tools.json`；檢查腳本使用原生 CMake／clang-format。

### 使用 Clang

同樣需要 Visual Studio C++ 工具與 Windows SDK；檢查腳本會自動載入 x64 開發環境。

```powershell
pwsh -File scripts/setup-tools.ps1 -WithClang
pwsh -File scripts/check.ps1 -Stage all -Compiler clang -Config Debug
pwsh -File scripts/check.ps1 -Stage all -Compiler clang -Config Release
./out/build/clang/Release/RollerCoasters.exe
```

只編譯可將 `-Stage all` 改為 `-Stage build`。MSVC 與 Clang 使用獨立的建置目錄。

## 操作

- World：雙指捲動旋轉；Shift + 左鍵拖曳或右鍵拖曳也可旋轉。
- 平移：Alt + 雙指捲動或 Alt + 右鍵拖曳。
- 縮放：雙指張開放大、捏合縮小；也可 Ctrl + 捲動。
- 重設：Shift + 左鍵雙擊或右鍵雙擊。
- 控制面板在視窗右側，深色霓虹風格，依功能分成五張卡片，由上到下為：
  `RIDE`（Run、前後步進、ArcLength、Speed）、`CAMERA`（World／Train／Top）、
  `TRACK`（Spline Type、Add／Delete Point、Load／Save／Reset、R±X／R±Z）、
  `LIGHTING`（Light+、Shadows、Headlight、Bloom、SSAO、Time）、`WORLD`（Water、Rain、Ocean Ride）。
  開關亮青色代表開啟、深色代表關閉；Load／Save 讀寫軌道文字檔。
- 光影：`Light+` 開啟 shader 逐像素光照與 shadow map 陰影，關閉即回到原本固定管線畫面。
  `Shadows`、`Headlight`（火車頭燈）、`Water`（島外海面波浪、反射與折射）、`Bloom`（泛光與 god rays）、`SSAO` 可個別開關；
  `Time` 滑桿 0–24 時移動太陽與月亮，夜晚最容易看到頭燈。顯卡不支援時自動退回原本畫法。
- 地面是 40×40 的草方塊島（16×16 程序生成像素貼圖，草頂與泥土側面），不需要圖檔。
- `Ocean Ride` 載入一條潛進海底的示範軌道：穿過環形閘門入水，經過珊瑚礁與巨蚌、海藻林、岩拱、有燈的石柱遺跡與階梯神殿、
  水母群、沉船，從石門進入只靠發光水晶照明的岩石隧道，再穿過環形閘門衝出水面回到島上。
  用 `Train` 相機觀看效果最好。海底是 5×5×5 方塊組成的世界，所有貼圖都在程式裡畫出；
  軌道周圍會挖空並保留淨空，三種 spline 下方塊與水草都不會碰到列車。
  海水清澈，遠景淡入藍色；鏡頭在水下時有海床光紋（caustics）、從水面射下的光束、頭燈光錐、漂浮微粒與氣泡，
  抬頭可看到水面的 Snell 窗與閃爍的太陽。海裡有繞著軌道的魚群、鯊魚、海龜、魟魚、鯨魚與發光水母。
- 水面：細分網格以 Gerstner 波做頂點位移，多層捲動噪點擾動法線，倒影先做螢幕空間反射（SSR），沒打中時退回平面反射，
  強度依菲涅耳（Schlick）公式隨視角變化。
- `Rain` 開啟下雨：天空轉陰、有雨絲，水面出現雨滴漣漪，地面逐漸變濕（變暗、高光變銳利），水窪會鏡面反射周圍景物；
  關閉後慢慢變乾。岸邊貼近水面的一圈永遠是濕的。

個人軌道請存 `data/`，備份存 `backups/`，兩者均不納入版控。

## 開發入口

```powershell
pwsh -File scripts/check.ps1 -Stage all -Config Debug
pwsh -File scripts/check.ps1 -Stage all -Config Release
```

- [開發設定與品質檢查](docs/development.md)
- [貢獻流程](CONTRIBUTING.md)
- [架構與資料流](docs/architecture.md)
- [發布、備份與手動驗證](docs/operations.md)
- [AI 開發規範](AGENTS.md)
