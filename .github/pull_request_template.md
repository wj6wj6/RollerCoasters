## 原因

關聯 Issue：

## 改前 → 改後

描述可觀察的行為變化；UI 變更附畫面或操作結果。

## 驗證

一次跑完下列四項：`pwsh -File scripts/check-all.ps1`，貼上 `out/check-summary.md` 的表格；或在 PR 加上 `windows-ci` 標籤讓 Windows CI 執行。

- [ ] Debug：`pwsh -File scripts/check.ps1 -Stage all -Config Debug`
- [ ] Release：`pwsh -File scripts/check.ps1 -Stage all -Config Release`
- [ ] Clang Debug：`pwsh -File scripts/check.ps1 -Stage all -Compiler clang -Config Debug`
- [ ] Clang Release：`pwsh -File scripts/check.ps1 -Stage all -Compiler clang -Config Release`
- [ ] 文件、架構圖與 ADR 已依變更同步（不適用請說明）

GUI／裝置驗證、未執行的檢查與原因：

## 風險與回復

資料格式／依賴／相容性影響、備份與回復方式；無影響亦請明確說明。
