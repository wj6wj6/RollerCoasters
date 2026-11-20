# 發布、備份與驗證

## 發布前

1. `pwsh -File scripts/check-all.ps1` 全部成功（等同 MSVC 與 Clang 各自 Debug／Release 的 `scripts/check.ps1 -Stage all`）。
2. 在實際 Windows 裝置上執行 `pwsh -File scripts/control-app.ps1 smoke`，再依 [驗證流程](../.agents/skills/verify-app/SKILL.md) 完成改動相關的手動檢查，於 PR 記錄結果。
3. 備份重要軌道檔並做一次還原驗證。
4. 需要發布新版本時先修改 `CMakeLists.txt` 的 `project(... VERSION ...)`，合併後再建立對應 `v*` tag。

## CI

repository 是公開的，GitHub 提供的標準 runner（含 Windows）不計費。CI 分成兩個 workflow，每個非 draft PR 的 push 都會兩個一起跑；只改文件（`*.md`、`docs/`）時略過：

| Workflow | 何時執行 | 內容 |
| --- | --- | --- |
| `Linux quality`（`ci.yml`，ubuntu-24.04，兩個平行的 job） | PR push、main push、手動 | 快速回饋。`linux`：`check.ps1 -Stage quick`（clang-format 22.1.8、依賴指紋、repository 規範與腳本語法）；`tests/linux/` 以 clang `-Wall -Werror` 編譯平台無關的原始碼並跑 `VersionCheck` 以外的全部 CTest 檢查（xvfb）。`win32-syntax`：`tests/linux/win32-syntax.sh` 用 MinGW-w64 對真正的 Windows SDK 標頭檢查所有檔案的語法 |
| `Windows build`（`windows.yml`，windows-2022，平行的 job） | PR push、main push、`v*` tag、手動 Run workflow | 完整把關，內容與 `scripts/check-all.ps1` 相同，拆成同時執行的 job：`tools` 先準備並快取工具鏈；`shared` 跑一次格式、repository 規範與 MSVC `/analyze`；`build` 的四個 matrix job（MSVC／Clang × Debug／Release）各自執行 `check-all.ps1 -NoShared`，即編譯、CTest、安裝與 `--version`；最後的 `windows` job 在全部通過時才通過（必要檢查設這個名稱即可） |

- 同一個 PR 或分支的新 push 會取消舊的執行，draft PR 不執行。
- Ubuntu 套件鏡像站有時慢到讓安裝逾時，所以兩個 Linux job 各自快取下載過的 `.deb`（`install-packages.sh --only core`／`--only mingw`，快取鍵含腳本的雜湊）。安裝一成功就存檔（不必等整個 job 成功），之後的執行不經網路直接安裝。PR 讀得到 main 存的快取，所以 main 跑過一次後 PR 都不必再下載。
- MinGW 佔下載量的大半，所以獨立成 `win32-syntax` job，`linux` 不必等它。
- 第一次（沒有快取）的下載仍可能很慢（曾遇到鏡像站每秒約 100 KB，MinGW 77 MB），所以安裝步驟的時限放寬（`linux` 20 分鐘、`win32-syntax` 25 分鐘），讓它至少完整下載一次並存成快取。
- `tests/linux/stubs/windows.h` 定義了 Windows SDK 會吃掉的 `far`、`near`、`DUPLICATE` 等巨集，所以這類名稱在 Linux 檢查就會失敗；沒有 Windows 的雲端環境也能在推送前跑這組檢查（見 [development.md](development.md#linux-上的檢查)）。
Linux 檢查不是 MSVC 建置：MSVC 專屬警告、`/analyze`、連結 Win32 程式與封裝只有 Windows build 或本機 `check-all.ps1` 會驗證。

在 Windows 電腦上執行 `pwsh -File scripts/check-all.ps1` 等同 Windows build，結果表寫在 `out/check-summary.md`，可貼進 PR。
若 repository 改回私有，Actions 會依分鐘計費（Windows 比 Linux 貴數倍，額度用完時連 Linux job 都不會開始）；那時應讓 Windows build 只在 main 與手動時執行，PR 以本機 `check-all.ps1` 驗證。
Windows build 的每個 matrix job 保存自己的 `out/check-summary.md` 與 JUnit／CTest 日誌（`test-results-windows-<編譯器>-<組態>`，14 天），Release 的兩個 job 另存已執行 `--version` 的安裝目錄（`RollerCoasters-windows-x64-msvc`／`-clang`，14 天）。拆成平行的 job 後，一次 Windows build 約 4–5 分鐘（單一 job 依序跑四個組態時約 6–7 分鐘）。Linux 檢查只在失敗時保存日誌（7 天）。
這是桌面應用程式，以 executable 的版本 smoke test 取代服務 healthcheck；沒有 Docker 或伺服器部署。

## 建立可交付目錄

```powershell
pwsh -File scripts/check.ps1 -Stage all -Config Release
./out/package/Release/RollerCoasters.exe --version
```

交付 `out/package/Release/`。目標機器需 Windows x64、支援 OpenGL 的顯示驅動，以及 Microsoft Visual C++ x64 Runtime（2015 之後的 v14 系列，需不舊於建置所用的 Visual Studio 版本）。
使用 Clang 封裝時加上 `-Compiler clang`，交付目錄改為 `out/package/clang/Release/`，執行期前置需求相同。
目前未包含安裝程式、程式簽章與自動更新；正式對外發布前應另行規劃。
交付目錄不含任何材質包，只有空的 `texturepacks/` 資料夾，使用者把自己的材質包放進去（見 README「材質包」）。
`--version` 驗證程式與執行期可啟動，無法驗證 GPU 呈現；在目標機器上用 `RollerCoasters.exe --doctor` 檢查 OpenGL、shader 路徑與資料夾。

## 自動驗證（control-app）

`scripts/control-app.ps1` 驅動已建置的程式（預設找 `out/` 下最新的 Release 建置，`-Exe` 可指定），證據寫到 `out/verify/`（不納入版控）：

| 指令 | 作用 |
| --- | --- |
| `doctor` | `--doctor`：TrackFiles 可寫入、材質包可開啟、OpenGL 與 GLSL 版本、shader 光影與後製是否可用（失敗時程式會靜默退回固定管線，畫面仍「看起來正常」）。 |
| `screenshot -Name <n> -AppArgs ...` | `--screenshot`：設定場景、畫完指定幀數後存 `out/verify/<n>.png`，並判斷畫面是否真的畫出東西（不是一片底色）。 |
| `smoke [-Only <前綴>]` | 依序跑全部情境（`list` 列出），包含預期失敗的壞軌道檔、缺檔與命令列錯誤；結果表寫在 `out/verify/smoke.txt`。 |
| `start -AppArgs ...`／`stop` | 以指定場景開啟視窗做手動檢查，之後關閉。 |
| `clean` | 關閉 `start` 開的程式並刪除 `out/verify/`。 |

程式本身的結束碼：0 通過、1 檢查失敗、2 命令列錯誤。場景參數見 `RollerCoasters.exe --help`。
限制：截圖只能證明畫面不是空白、OpenGL 路徑正確，不能判斷景物是否擺對；這部分仍要看截圖或手動檢查。CI 的 Windows runner 沒有 GPU（只有 OpenGL 1.1），所以 `smoke` 目前不在 CI 執行，需要在有顯示卡的 Windows 電腦上跑。

## GUI 驗證

手動檢查項目依功能拆在 [功能地圖](../.agents/skills/verify-app/references/features/README.md)，每份功能檔也列出可自動化的情境與容易騙過檢查的假象；流程見 [verify-app](../.agents/skills/verify-app/SKILL.md)。
日常改動只做相關功能檔的檢查；發布前把全部功能檔都做一遍，並記錄 Windows 版本、GPU／驅動與輸入裝置。

## 軌道備份與還原

Load／Save 會直接讀寫指定檔案。儲存前保留原檔，尤其不要用唯一的資料副本測試新版本。

```powershell
New-Item -ItemType Directory -Force backups | Out-Null
Copy-Item -LiteralPath data/my-track.txt -Destination backups/my-track-before-change.txt
Get-FileHash -Algorithm SHA256 data/my-track.txt, backups/my-track-before-change.txt
Copy-Item -LiteralPath backups/my-track-before-change.txt -Destination data/my-track-restored.txt
Get-FileHash -Algorithm SHA256 backups/my-track-before-change.txt, data/my-track-restored.txt
```

確認雜湊相同，再於程式載入 `my-track-restored.txt`，確認控制點和畫面。
還原使用新檔名，驗證後才決定是否替換工作檔。重要備份另存其他磁碟；repository 的 ignore 不能代替備份。
若未來增加格式版本，以追加且相容舊資料的方式處理，先加入舊格式讀取與新格式往返測試。

## 回復版本與排錯

保留前一版可用的發布目錄；新版本異常時關閉程式，使用前一版 executable 與已驗證的軌道備份。
建置失敗先確認使用 x64 MSVC／clang-cl 與 `tools.json` 指定的原生工具版本。內附 FLTK 1.3.11 以 MSVC 19.44（VS 2022 17.14）編譯，連結時出現與 FLTK `.lib` 有關的未解析符號或「由較新編譯器建立」時，更新 Visual Studio，或執行 `pwsh -File scripts/build-fltk.ps1` 用本機版本重新編譯（之後 `-Stage vendor-lock`）。Clang 模式先執行 `scripts/setup-tools.ps1 -WithClang`。不要混用 MinGW 或其他架構的 libraries。
CI 失敗保留 test-results artifact；日誌與 issue 附件避免包含使用者私密路徑與軌道內容。
