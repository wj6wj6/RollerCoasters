# 發布、備份與驗證

## 發布前

1. MSVC 與 Clang 各自 Debug／Release 的 `scripts/check.ps1 -Stage all` 全部成功。
2. 使用下列 GUI 清單驗證實際 Windows 裝置，於 PR 記錄結果。
3. 備份重要軌道檔並做一次還原驗證。
4. 需要發布新版本時先修改 `CMakeLists.txt` 的 `project(... VERSION ...)`，合併後再建立對應 `v*` tag。

PR、main push、`v*` tag 與手動觸發都執行同一個 Windows CI，tag 發布亦重新檢查。`tools` job 先準備並快取 `tools.json` 指定的工具及檢查格式，四個 `quality` job 再共用快取執行其餘檢查。
CI 會保存測試 JUnit／CTest 日誌，以及已執行 `--version` 的安裝目錄 artifact。
這是桌面應用程式，以 executable 的版本 smoke test 取代服務 healthcheck；沒有 Docker 或伺服器部署。

## 建立可交付目錄

```powershell
pwsh -File scripts/check.ps1 -Stage all -Config Release
./out/package/Release/RollerCoasters.exe --version
```

交付 `out/package/Release/`。目標機器需 Windows x64、支援 OpenGL 的顯示驅動，以及 Microsoft Visual C++ 2015–2022 x64 Runtime。
使用 Clang 封裝時加上 `-Compiler clang`，交付目錄改為 `out/package/clang/Release/`，執行期前置需求相同。
目前未包含安裝程式、程式簽章與自動更新；正式對外發布前應另行規劃。
`--version` 驗證程式與執行期可啟動，無法驗證 GPU 呈現。

## GUI 驗證清單

- 正常啟動，可見軌道與控制點，視窗縮放後繪圖正常。
- 切換 World／Train／Top 相機，Run、速度與前後步進操作正常。
- 雙指旋轉平滑、水平線穩定；捏合／張開與 Ctrl + 滾輪縮放方向正確。
- Alt 平移與重設正常；重設後沒有殘留動畫。
- 用測試軌道複本執行 Load／Save，再載入新檔，點數、位置與方向一致。
- `Light+` 開關切換時畫面正常；`Time` 從 0 拖到 24，太陽、陰影、天空與夜晚星空連續變化。
- `Shadows`、`Headlight`、`Water`、`Bloom`、`SSAO` 逐一開關，三種相機下都沒有黑畫面或閃爍；開啟後仍能點選與拖曳控制點。
- 按 `Ocean Ride` 後用 `Train` 相機跑完一圈：入水與出水時畫面切換正常，水下有藍霧、光束、微粒與氣泡，隧道內變暗只剩水晶與頭燈，列車不會穿過方塊；三種 spline 都試一次。
- 關閉視窗可結束程序；記錄 Windows、GPU／驅動與輸入裝置。

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
建置失敗先確認使用 x64 MSVC／clang-cl 與 `tools.json` 指定的原生工具版本。Clang 模式先執行 `scripts/setup-tools.ps1 -WithClang`。不要混用 MinGW 或其他架構的 libraries。
CI 失敗保留 test-results artifact；日誌與 issue 附件避免包含使用者私密路徑與軌道內容。
