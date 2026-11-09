# 貢獻流程

先讀 [開發設定](docs/development.md)，並安裝 repository hooks。

1. 從最新的 `main` 建立單一目的分支，例如 `fix/camera-zoom`。
2. Issue 說明問題、重現方式與預期行為；PR 描述原因、改前改後、驗證與風險。
3. 修改行為時加入能驗證該行為的測試。純文件或機械格式調整使用相應檢查即可。
4. 跑完 MSVC 與 Clang 各自 Debug／Release 的 `all` 檢查（Clang 加 `-Compiler clang`）；顯示或裝置輸入變更另做 GUI 驗證。
5. 提交使用 `fix: ...`、`feat: ...`、`docs: ...`、`ci: ...` 等 Conventional Commits 標題。
6. 一個變更維持一個 PR，review 修正追加到同一分支。合併前確認 CI 全部通過。

維護者應在 GitHub 的 `main` branch ruleset 設定要求 PR 與 `Windows quality` 的 `tools` 檢查及四個 `quality` matrix 檢查（MSVC／Clang × Debug／Release）。`tools` 失敗時 matrix 會被略過，因此 `tools` 也必須列為必要檢查。
repository 內的 workflow 提供這些檢查；遠端保護規則需由維護者在 GitHub 設定。

更新工具時，以獨立變更更新 `tools.json` 的版本、官方下載 URL 與 SHA256。更新內附函式庫時說明來源、版本與 MSVC ABI 相容性，重新產生 `vendor-lock.json` 並跑完整檢查。
