# 開發設定與品質檢查

## 環境

安裝 Visual Studio 2022 或更新版本的 C++ 桌面開發工具（MSVC、Windows SDK、C++ CMake tools）與 PowerShell 7.4+。
CMake、clang-format 的版本與官方下載 SHA256 固定在 `tools.json`。
Clang 建置所用的 clang-cl 與 Ninja 也記錄於同一個檔案。

```powershell
pwsh -File scripts/setup-tools.ps1
```

準備腳本先檢查現有原生工具的版本；缺少指定版本時，從官方 release 下載至 `out/`，驗證 SHA256 後解壓縮。只影響專案目錄，不修改系統 PATH，也不需要系統管理員權限。
檢查腳本優先使用 `out/tools/`，其次使用 PATH 上版本相符的工具；版本不符即失敗。
CMake 官方套件：[4.3.0 release](https://github.com/Kitware/CMake/releases/tag/v4.3.0)；clang-format 官方套件：[LLVM 22.1.8 release](https://github.com/llvm/llvm-project/releases/tag/llvmorg-22.1.8)。
LLVM 官方封存檔約 822 MB，首次下載較久，僅解出 clang-format.exe。解壓縮固定使用 Windows 內建的 `tar.exe`；若該版本不支援 `.xz`，改用 7-Zip 先解壓一次。Windows CI 快取解壓後的工具；Linux CI 從 PyPI 安裝同版本的 clang-format（`check.ps1` 在 Linux 上從 PATH 找不帶 `.exe` 的工具）。

需要 Clang 時，使用 `pwsh -File scripts/setup-tools.ps1 -WithClang`，會額外準備 Ninja、Clang 編譯器、原生 DLL 與 builtin headers。
[clang-cl](https://clang.llvm.org/docs/UsersManual.html#clang-cl) 使用 MSVC 相容的命令列與 ABI，仍需要 Visual Studio C++ 標頭、連結器、執行期與 Windows SDK。
此模式不使用 MinGW 的 C++ ABI。檢查腳本會透過 `vswhere`／`VsDevCmd.bat` 載入 x64 環境，設定只作用於該次腳本程序。

尚未做到整個 Windows 工具鏈位元一致。CI 固定 `windows-2022` runner 系列，實際 runner image、PowerShell 與 MSVC 修訂版由 GitHub 維護。
CMake preset 不寫死 Visual Studio 版本（見 [ADR 0004](adr/0004-any-visual-studio-version.md)）：
- `vs`：給 Visual Studio「開啟資料夾」與 VS Code CMake Tools 用。Ninja Multi-Config＋`cl.exe`，`architecture`／`toolset` 設為 `external`，由 IDE 依已安裝的 Visual Studio 載入 x64 環境；輸出到 `out/build/vs/`，build preset `vs-debug`／`vs-release`。
- `windows`：命令列與 CI 用，不指定 generator，CMake 自動選用已安裝的最新 Visual Studio（MSBuild），固定 x64、C++17、Debug／Release。
- `analyze` 繼承 `windows` 並開啟 MSVC `/analyze`。
- 個人化設定（例如指定某一版 Visual Studio）放在未納入版控的 `CMakeUserPresets.json`。
`clang` preset 使用 clang-cl 與 Ninja Multi-Config，輸出到獨立的 `out/build/clang/`。
若直接執行 `cmake --preset clang`，請先進入 x64 Native Tools shell，並將指定版本的 clang-cl 與 Ninja 加入 PATH；一般開發可直接使用下方腳本。

`include/` 與 `lib/` 是已入版控的第三方依賴，指紋記錄於 `vendor-lock.json`。
指紋按相對路徑的 ordinal 順序與內容計算；文字 CRLF 正規化為 LF，`.lib` 使用原始 bytes。
這可偵測檔案增刪改；它記錄的是本 repository 現況，不能證明原始供應鏈來源。

## 同一套檢查

合併前一次跑完下面四個組態（格式與 `/analyze` 只跑一次），並把結果表寫到 `out/check-summary.md`：

```powershell
pwsh -File scripts/check-all.ps1          # 加 -Jobs 0 用上所有核心；-Compiler／-Config 只跑其中幾組，-NoShared 略過格式與 /analyze（CI 的 matrix job 用）
```

也可以逐一執行：

```powershell
pwsh -File scripts/check.ps1 -Stage all -Config Debug
pwsh -File scripts/check.ps1 -Stage all -Config Release
pwsh -File scripts/check.ps1 -Stage all -Compiler clang -Config Debug
pwsh -File scripts/check.ps1 -Stage all -Compiler clang -Config Release
```

`all` 依序執行格式、repository 規範與腳本語法、MSVC 靜態分析、一般編譯、CTest、安裝與已安裝程式的版本檢查。任一步驟失敗立即停止。
repository 規範另外檢查曾經漏掉過的同步：`src/` 的 `.cpp` 都在 Windows 目標的清單裡（Linux 建置自動收集，抓不到這種漏列）、每個 `tests/*.cpp` 都在兩個 CMakeLists 和下方「測試範圍」裡、每個原始檔在功能地圖的對照表裡都有一列、文件沒有超過 800 字的單行。再犯同一種錯時，優先把它加成這裡的檢查，而不是在 AGENTS.md 多寫一條規則。
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

`check.ps1` 預設只用一個建置程序，避免筆電卡住；`-Jobs N` 改為 N 個，`-Jobs 0` 用上所有處理器（CI 使用）。

所有第一方 C++ 編譯使用 `/W3 /WX`，分析使用 `/analyze /analyze:external-`；第三方 GLAD 分開建置。
隨附 FLTK 1.3.11 的 Debug `.lib` 內含除錯資訊（`/Z7`），連結器不再需要抑制 LNK4099；VS 偵錯時呼叫堆疊可看到 FLTK 內部。重新編譯內附 FLTK 用 `pwsh -File scripts/build-fltk.ps1`，再 `-Stage vendor-lock`（見 `docs/adr/0007-fltk-1-3-11.md`）。連結內附 FLTK 需要 MSVC 19.44（VS 2022 17.14）或更新。
初次導入 clang-format 會建立全體 `src/`、`tests/` 的一致格式基準；第三方檔案不重排。

## Hooks

```powershell
git config core.hooksPath .githooks
```

pre-commit 執行 `quick`；commit-msg 檢查 Conventional Commits；pre-push 執行完整 Debug 檢查。
hooks 與 CI 使用同一個 PowerShell 入口與 `tools.json`。hooks 只檢查、不修改檔案。CI 的分工與觸發條件見 [operations.md](operations.md#ci)。

## Linux 上的檢查

沒有 Windows 時（例如雲端環境），可以先跑 Linux CI 的同一組檢查：

```sh
tests/linux/install-packages.sh           # Ubuntu 24.04：apt 套件與 tools.json 版本的 clang-format（--only core 不裝 MinGW）
pwsh -File scripts/check.ps1 -Stage quick
CC=clang CXX=clang++ cmake -S tests/linux -B out/build/linux -G Ninja
cmake --build out/build/linux
xvfb-run -a ctest --test-dir out/build/linux --output-on-failure
tests/linux/win32-syntax.sh
```

`tests/linux/` 只用系統的 FLTK 與 `stubs/` 裡的 `<windows.h>` 替身，編譯 `main.cpp`、`TrainView.cpp`、`TouchpadGestures.cpp` 以外的原始碼與全部檢查程式；這三個檔案只做 MinGW 語法檢查。新增測試時，在 `CMakeLists.txt` 與 `tests/linux/CMakeLists.txt` 都加上同樣的來源檔。
撤銷本機設定可執行 `git config --unset core.hooksPath`。

## 測試範圍

- `GestureMotionCheck`：正式 `platform::` event loop／timer（目前由 FLTK 實作）與 `ArcBallCam`，以 `InputEvent` 送入事件，涵蓋手勢平滑度、總旋轉量、重設、水平線、縮放、俯仰限制、觸控板細小位移立即套用、空白處拖曳旋轉、旋轉與縮放同時作用及拖曳中縮放。
- `FlyCamCheck`：正式 `platform::` event loop／timer 與 `FlyCam`，以 `InputEvent` 送入事件，涵蓋由 World 視圖矩陣接續、視角差補償、WASD／空白鍵／Shift／QE 依實際時間移動與放開即停、Ctrl 加速、方向鍵轉向與俯仰限制、拖曳看、滑鼠看、滾輪調速、觸控板捲動／捏合／平移、速度按鍵與失去焦點時停止、地面（含 `setGround()` 降低與升高後不跳動）與場景範圍限制、視野設定與上下限、飛行按鍵的掃描碼對應，以及 `lookAt()` 走短邊平滑轉向且使用者轉頭時立即讓出。
- `WaterFlowCheck`：水流規則（`src/WaterFlow.*`）：平地擴散 7 格、方向權重（999、落差在鄰格下方為 0、4 格內最短路徑）、只往最近落差流、超出 4 格時平均擴散、繞牆搜尋、落地後從水位 1 重新擴散、大海吸收。
- `FrustumCheck`：分塊剔除用的視錐判斷（`src/Frustum.*`）：透視視角前方的盒子看得到，後方、遠平面外、側邊外的看不到，跨過邊界的仍算看得到；正交（陰影 pass）視角移動後跟著移動。
- `ChaseRigCheck`：`Chase` 相機取景（`src/ChaseRig.*`）：不同列車長度、乘客高度與視野下，最後一位乘客的頭在畫面下半約一半處、列車前端在畫面內；列車越長退得越遠、乘客越高架得越高；有天花板時相機不高於天花板、不比下限更近，天花板比乘客頭高一格半以上時頭仍在畫面中線以下；平滑跟隨朝目標前進且不超過。
- `MobsCheck`：島上動物的模型與皮膚（`src/Mobs.*`）：羊、豬、牛每個方塊的每一面都落在內建皮膚有上色的位置（材質包同樣配置的皮膚因此也對得上），站立方塊的大小與貼圖配置一致，各有四條腿；只接受方塊遊戲的皮膚尺寸（64 x 32 的倍數，或 64 x 64 配置）。
- `LandRideClearanceCheck`：三種 spline 下島上的方塊與植物不碰到列車，軌道左右 6、下方 6 到上方 10 單位內沒有水。
- `HeadLookCheck`：Train 視角轉頭（`src/Utilities/HeadLook.*`）：起始看正前方、yaw 正值往右、pitch 停在上下限、yaw 跨過 ±180 度回繞、可轉到正後方，視線上方始終與視線垂直且朝上。
- `GuiCheck`：方塊遊戲風介面（`src/Ui/*`）不需 OpenGL：像素字型的寬度、下伸與未知字元、atlas 內容與配置、自動 GUI 縮放，以及按鈕按下即觸發且只觸發一次、空白處不吃事件、停用的按鈕不觸發、開關翻轉、循環按鈕左鍵往後右鍵往前並回繞、滑桿按下設值、拖出範圍時夾住、放開後停止、清單滾輪捲動到底即停、點選滑鼠下的列。
- `EditKeysCheck`：控制點編輯鍵位對照正式 `FlyCam`：Fly 下沒有飛行鍵是編輯指令、Fly 的編輯鍵都是 `FlyCam` 不用的鍵、World／Top 的鍵位不變。
- `GizmoDragCheck`：移動把手（`src/TransformGizmo.*`）找得到滑鼠下的把手，拖曳時點正好落在滑鼠下，且只沿把手允許的軸或平面移動。
- `TrackInsertCheck`：新增控制點（`src/TrackInsert.*`）後軌道仍經過點擊處、新點插在前後兩點之間並內插傾斜、列車留在原位、螢幕上找得到滑鼠下的軌道位置。
- `TrackHistoryCheck`：上一步／下一步（`src/TrackHistory.*`）回到當時的軌道與選取、下一步能前進、新修改清掉下一步、點數不變時列車留在原位、只保留最近的步數。
- `TrackSpacingCheck`：弧長表讓枕木等距，控制點分佈很不平均時，以獨立的細分弧長量測誤差仍在 2% 以內。
- `OceanRideClearanceCheck`：三種 spline 下海底的方塊與植物不碰到列車，以正式 `CTrack` 取樣，與挖空景物的程式互相獨立。
- `TexturePackCheck`：zip 的三種 DEFLATE 區塊解碼與截斷資料、PNG 解碼與取動畫第一格、zip 與資料夾材質包的開啟、替代與染色、不是材質包的資料夾被拒絕、依名稱列出材質包。
- `TwoFingerGestureCheck`：觸控板兩指濾波，驗證靜止不動、純捏合不轉動、純移動不縮放、兩者明顯時同時作用。
- `TrackRoundTripCheck`：正式 `CTrack::writePoints/readPoints` 與真實暫存檔，驗證座標、方向、控制點數與載入後列車位置歸零；只有 x y z 的舊檔仍可載入；點數不合法、空檔、點的行數少於宣告或某行缺 x y z 時，原軌道與列車位置不變，並且只跳出一次 `fl_alert`（測試以 FLTK timer 關閉正式的對話框）。
- `AppControlCheck`：命令列解析與錯誤、`checkTrackFile()` 與正式 `CTrack::readPoints()` 對同一份檔案的結果一致、PNG 寫入後以材質包用的同一個解碼器讀回逐像素相同、空白畫面不被當成已畫出、`--doctor` 的資料夾檢查（隔離的暫存資料夾）。
- `VersionCheck`：啟動正式 executable 的 `--version`，不建立 GUI。

測試有明確 timeout，找不到測試時視為失敗；Release 的檢查不用會被移除的 `assert`。
相機測試使用真實時間；忙碌機器可能受排程影響，失敗時檢查 CTest 日誌，保留原失敗證據。
這些測試未涵蓋實際 GPU 畫面與 Windows 觸控訊息，也未涵蓋數字以外的文字（例如 `a b c` 仍讀成 0）等其他損壞軌道檔。實際畫面用 `scripts/control-app.ps1` 在 Windows 上截圖驗證，觸控與手感仍需手動檢查；兩者都依 [驗證流程](../.agents/skills/verify-app/SKILL.md)。
