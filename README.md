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
- 控制面板提供 Run、速度、相機與控制點操作；Load／Save 讀寫軌道文字檔。
- 光影：`Light+` 開啟 shader 逐像素光照與 shadow map 陰影，關閉即回到原本固定管線畫面。
  `Shadows`、`Headlight`（火車頭燈）、`Water`（島外海面反射與折射）、`Bloom`（泛光與 god rays）、`SSAO` 可個別開關；
  `Time` 滑桿 0–24 時移動太陽與月亮，夜晚最容易看到頭燈。顯卡不支援時自動退回原本畫法。
- 地面是 40×40 的草方塊島（16×16 程序生成像素貼圖，草頂與泥土側面），不需要圖檔。

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
