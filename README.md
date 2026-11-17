# RollerCoasters

電腦圖學課程的 Windows 桌面過山車編輯器，使用 C++、FLTK 1.3.5 與 OpenGL。

## 建置與啟動

安裝 Visual Studio 2022 或更新版本（例如 2026）的 **使用 C++ 的桌面開發**（MSVC、Windows SDK、C++ CMake tools），以及 PowerShell 7.4 以上版本。

最簡單的方式：clone 後在 Visual Studio 選「開啟資料夾」（或在檔案總管對資料夾按右鍵「使用 Visual Studio 開啟」），
Visual Studio 會讀取 `CMakePresets.json`，選 `Visual Studio (any version) / x64` 與 `Debug`，啟動項目選 `RollerCoasters.exe` 後按 F5。
VS Code 搭配 CMake Tools 擴充也一樣選這個 preset。這個 preset 不綁定 Visual Studio 版本，由 IDE 自行載入已安裝的 x64 編譯器。

命令列建置與完整檢查，以下命令均在此 repository 根目錄執行：

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

- World：觸控板雙指移動旋轉（滑鼠滾輪左右傾斜也可）；在空白處單指（左鍵）拖曳、Shift + 左鍵拖曳或右鍵拖曳也可旋轉。
- 平移：Alt 或 Shift + 雙指捲動；Alt + 在空白處左鍵拖曳或 Alt + 右鍵拖曳。
- 縮放：滑鼠滾輪向上放大、向下縮小；觸控板雙指張開放大、捏合縮小。觸控板可以一邊移動雙指旋轉、一邊捏合縮放；拖曳旋轉或平移時也能用滾輪縮放。
- 重設：Shift + 左鍵雙擊或右鍵雙擊。
- Train（坐在列車上）：可以轉頭看四周，視線跟著列車前進與轉彎。
  - 轉頭：在畫面上用左鍵或右鍵拖曳（往右拖看右邊、往上拖看上面），觸控板單指按住拖曳或雙指移動也可以（雙指時畫面跟著手指走，和 World、Fly 一樣，放開就停）。左右可以轉一整圈往後看，上下最多 85 度。
  - 回到正前方：雙擊畫面，或按 R／Home（R 依實體按鍵位置讀取，開著注音也有效）。每次切換到 Train 都從正前方開始。
- Fly（觀察者模式，操作仿照方塊遊戲的觀察者模式）：第一次切換時從目前 World 視角出發，之後切換相機會保留位置。
  - 滑鼠看：在畫面空白處點一下，滑鼠游標隱藏，移動滑鼠（或在觸控板上滑一根手指）直接轉視角；按 Esc 放開滑鼠。點到控制點仍是選取與拖曳。
  - 移動：按住 W／A／S／D 水平前後左右（低頭時也不會鑽進地面）、空白鍵上升、Shift 下降（E／Q、Page Up／Page Down 也可以）；按住 Ctrl 加速 2.5 倍。
  - 輸入法：Fly 模式會暫時關閉這個視窗的輸入法（例如注音），離開 Fly 自動恢復；W／A／S／D、空白鍵、Shift、Ctrl、E／Q、F、R 依實體按鍵位置讀取，所以中文輸入法或其他鍵盤配置下也能飛。
  - 速度：滑鼠滾輪往上變快、往下變慢；也可用 +／- 鍵或面板的 fly speed（每秒移動單位）。
  - 其他看法：右鍵拖曳、方向鍵、觸控板雙指移動（放開手指就停，不會慣性旋轉）。
  - 觸控板：雙指張開／捏合（或 Ctrl + 捲動）沿視線前進後退，可以一邊轉頭一邊前進；Alt + 雙指移動左右、上下平移。
  - 找方向：畫面中央有準星；地面延伸到地平線。按 F 平滑轉向列車，按 R 回到 World 視角位置。
  - 範圍：相機最低可以飛到 y = −200（可以鑽到地面以下看潛入地底或海底的軌道，從下方也看得到地面），最高 400，且不會離場景中心超過 400 單位。
- 視野：面板的 FOV 滑桿調整目前相機的垂直視野（30–120 度）。World、Train、Fly 各自記住自己的視野，預設分別是 40、60、90 度；Top 是正交投影，切到 Top 時滑桿停用。
- 控制面板提供 Run、速度、相機與控制點操作；Load／Save 讀寫軌道文字檔。

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
