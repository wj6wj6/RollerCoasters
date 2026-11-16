# 開發設定與品質檢查

## 環境

安裝 Visual Studio 2022／MSVC v143、Windows SDK 與 PowerShell 7.4+。
CMake、clang-format 的版本與官方下載 SHA256 固定在 `tools.json`。
Clang 建置所用的 clang-cl 與 Ninja 也記錄於同一個檔案。

```powershell
pwsh -File scripts/setup-tools.ps1
```

準備腳本先檢查現有原生工具的版本；缺少指定版本時，從官方 release 下載至 `out/`，驗證 SHA256 後解壓縮。只影響專案目錄，不修改系統 PATH，也不需要系統管理員權限。
檢查腳本優先使用 `out/tools/`，其次使用 PATH 上版本相符的工具；版本不符即失敗。
CMake 官方套件：[4.3.0 release](https://github.com/Kitware/CMake/releases/tag/v4.3.0)；clang-format 官方套件：[LLVM 22.1.8 release](https://github.com/llvm/llvm-project/releases/tag/llvmorg-22.1.8)。
LLVM 官方封存檔約 822 MB，首次下載較久，僅解出 clang-format.exe。解壓縮固定使用 Windows 內建的 `tar.exe`；若該版本不支援 `.xz`，改用 7-Zip 先解壓一次。CI 由單一 `tools` job 下載並快取解壓後的工具，同時檢查格式；四個 `quality` matrix job 只還原同一份快取。

需要 Clang 時，使用 `pwsh -File scripts/setup-tools.ps1 -WithClang`，會額外準備 Ninja、Clang 編譯器、原生 DLL 與 builtin headers。
[clang-cl](https://clang.llvm.org/docs/UsersManual.html#clang-cl) 使用 MSVC 相容的命令列與 ABI，仍需要 Visual Studio C++ 標頭、連結器、執行期與 Windows SDK。
此模式不使用 MinGW 的 C++ ABI。檢查腳本會透過 `vswhere`／`VsDevCmd.bat` 載入 x64 環境，設定只作用於該次腳本程序。

尚未做到整個 Windows 工具鏈位元一致。CI 固定 `windows-2022` runner 系列，實際 runner image、PowerShell 與 MSVC 修訂版由 GitHub 維護。
CMake 的 `windows` preset 固定 x64、C++17、Debug／Release；`analyze` preset 開啟 MSVC `/analyze`。
`clang` preset 使用 clang-cl 與 Ninja Multi-Config，輸出到獨立的 `out/build/clang/`。
若直接執行 `cmake --preset clang`，請先進入 x64 Native Tools shell，並將指定版本的 clang-cl 與 Ninja 加入 PATH；一般開發可直接使用下方腳本。

`include/` 與 `lib/` 是已入版控的第三方依賴，指紋記錄於 `vendor-lock.json`。
指紋按相對路徑的 ordinal 順序與內容計算；文字 CRLF 正規化為 LF，`.lib` 使用原始 bytes。
這可偵測檔案增刪改；它記錄的是本 repository 現況，不能證明原始供應鏈來源。

## 同一套檢查

```powershell
pwsh -File scripts/check.ps1 -Stage all -Config Debug
pwsh -File scripts/check.ps1 -Stage all -Config Release
pwsh -File scripts/check.ps1 -Stage all -Compiler clang -Config Debug
pwsh -File scripts/check.ps1 -Stage all -Compiler clang -Config Release
```

`all` 依序執行格式、repository 規範與腳本語法、MSVC 靜態分析、一般編譯、CTest、安裝與已安裝程式的版本檢查。任一步驟失敗立即停止。
`-Compiler` 預設為 `msvc`；設為 `clang` 時，編譯、測試與封裝都使用 clang-cl 的產物。`lint` 仍執行共用 MSVC `/analyze`，不將它當成 Clang Static Analyzer。
MSVC 發布目錄是 `out/package/<Config>/`，Clang 是 `out/package/clang/<Config>/`。

| 檢查 | 單獨命令（前綴均為 `pwsh -File scripts/check.ps1`） |
| --- | --- |
| C++ 格式 | `-Stage format`；自動修正加 `-Fix` |
| 依賴／資料政策、腳本語法與 MSVC 靜態分析 | `-Stage lint -Config Debug` |
| C++ 型別、警告與連結 | `-Stage build -Config Debug` |
| 正式元件整合與 smoke tests | `-Stage test -Config Debug`（先 build） |
| 安裝與版本驗證 | `-Stage package -Config Debug`（先 build） |
| 快速檢查 | `-Stage quick`（格式、repository 政策與腳本語法） |

所有第一方 C++ 編譯使用 `/W3 /WX`，分析使用 `/analyze /analyze:external-`；第三方 GLAD 分開建置。
隨附 Debug FLTK 沒有 PDB，因此僅抑制該組態的 linker LNK4099。
初次導入 clang-format 會建立全體 `src/`、`tests/` 的一致格式基準；第三方檔案不重排。

## Hooks

```powershell
git config core.hooksPath .githooks
```

pre-commit 執行 `quick`；commit-msg 檢查 Conventional Commits；pre-push 執行完整 Debug 檢查。
hooks 與 CI 使用同一個 PowerShell 入口與 `tools.json`。hooks 只檢查、不修改檔案；CI 同時驗證 MSVC／Clang 的 Debug 與 Release。
撤銷本機設定可執行 `git config --unset core.hooksPath`。

## 測試範圍

- `GestureMotionCheck`：正式 FLTK event loop／timer 與 `ArcBallCam`，涵蓋手勢平滑度、總旋轉量、重設、水平線、縮放、俯仰限制、觸控板細小位移立即套用、空白處拖曳旋轉、旋轉與縮放同時作用及拖曳中縮放。
- `FlyCamCheck`：正式 FLTK event loop／timer 與 `FlyCam`，涵蓋由 World 視圖矩陣接續、視角差補償、WASD／QE 依實際時間移動與放開即停、Shift 加速、方向鍵轉向與俯仰限制、拖曳看、觸控板捲動／捏合／平移、速度按鍵與失去焦點時停止、地面與場景範圍限制，以及 `lookAt()` 走短邊平滑轉向且使用者轉頭時立即讓出。
- `TwoFingerGestureCheck`：觸控板兩指濾波，驗證靜止不動、純捏合不轉動、純移動不縮放、兩者明顯時同時作用。
- `TrackRoundTripCheck`：正式 `CTrack::writePoints/readPoints` 與真實暫存檔，驗證座標、方向、控制點數與載入後列車位置歸零。
- `VersionCheck`：啟動正式 executable 的 `--version`，不建立 GUI。

測試有明確 timeout，找不到測試時視為失敗；Release 的檢查不用會被移除的 `assert`。
相機測試使用真實時間；忙碌機器可能受排程影響，失敗時檢查 CTest 日誌，保留原失敗證據。
這些測試未涵蓋實際 GPU 畫面、Windows 觸控訊息與損壞軌道檔的完整處理，手動流程見 [維運文件](operations.md)。
