# 開發規範

此 Git repository 的根目錄是 `RollerCoasters/`；上一層是課程素材。

## 工作流程

- 一件事使用一個分支與一個 PR；後續修正沿用原 PR。提交標題遵循 Conventional Commits。
- 開始前檢查 `git status`，保留使用者尚未提交的變更。
- 開發設定與工具問題先讀 `docs/development.md`；UI 操作查 `README.md`。
- 修改模組責任、事件流程或資料流時，同步更新 `docs/architecture.md` 的 Mermaid 圖；改變重要技術決策時追加 `docs/adr/`。
- 修改建置、封裝或發布流程時，同步更新 `docs/operations.md`。

## 必守規則

- 使用原生 CMake、clang-format 與 PowerShell 7.4+；工具版本以 `tools.json` 為準，透過 `scripts/setup-tools.ps1` 準備。
- Clang 建置使用 `clang-cl` 與 Ninja，先執行 `scripts/setup-tools.ps1 -WithClang`；保持 Windows x64／MSVC ABI 與內附 FLTK 相容。
- `src/` 與 `tests/` 使用 clang-format；第三方 `include/`、`lib/` 保留原始內容。
- 更新第三方檔案需說明來源與相容性，並用 `pwsh -File scripts/check.ps1 -Stage vendor-lock` 更新指紋。
- 編譯器警告視為錯誤。修正原因；新增抑制需在附近註解原因及範圍。
- 相機測試保留正式 `ArcBallCam`、FLTK event loop 與 timer 呼叫路徑；軌道測試使用正式讀寫函式和隔離的暫存檔。
- 軌道格式變更維持舊檔可讀；先備份真實軌道資料，再驗證還原。
- 執行資料放 `data/`，備份放 `backups/`；金鑰、私密設定及執行產物留在版控之外。

## 完成條件

1. 執行 `pwsh -File scripts/check.ps1 -Stage all -Config Debug`。
2. 執行 `pwsh -File scripts/check.ps1 -Stage all -Config Release`。
3. 另以 `-Compiler clang` 執行相同兩個組態；MSVC、Clang 的編譯、CTest 與安裝後 `--version` 全部成功，格式與共用 MSVC 靜態分析成功。
4. 涉及顯示或輸入的修改，另依 `docs/operations.md` 完成 GUI 手動檢查，記錄裝置與結果。
5. 文件與變更同步，說明改前改後、驗證及已知限制。未執行的檢查如實列出。
