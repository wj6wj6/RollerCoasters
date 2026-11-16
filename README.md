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

- World：雙指捲動旋轉；在空白處單指（左鍵）拖曳、Shift + 左鍵拖曳或右鍵拖曳也可旋轉。
- 平移：Alt 或 Shift + 雙指捲動；Alt + 在空白處左鍵拖曳或 Alt + 右鍵拖曳。
- 縮放：雙指張開放大、捏合縮小；也可 Ctrl + 捲動。
- 重設：Shift + 左鍵雙擊或右鍵雙擊。
- Fly（觀察者模式）：第一次切換時從目前 World 視角出發，之後切換相機會保留位置。
  - 移動：按住 W／S 前進後退（沿視線方向）、A／D 左右平移、E／Q 上升下降（或 Page Up／Page Down）；按住 Shift 三倍速。
  - 看：在空白處左鍵拖曳或右鍵拖曳、方向鍵、觸控板雙指捲動。
  - 觸控板：雙指張開／捏合（或 Ctrl + 捲動）前進後退；Alt 或 Shift + 雙指捲動左右、上下平移。滑鼠滾輪前進後退。
  - 速度：面板的 fly speed（每秒移動單位）或 +／- 鍵；R 回到 World 視角位置。
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
