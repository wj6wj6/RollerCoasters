# ADR 0001：沿用 Windows 工具鏈並集中品質檢查

日期：2026-11-02
狀態：採用

## 背景

專案內附 FLTK 1.3.5、GLM 0.9.8.5、GLAD 與 Windows `.lib`，已有正式 FLTK 相機測試，缺少 CI、工具版本與開發文件。

## 決策

使用 Windows x64／Visual Studio 2022、C++17 與 CMake presets。原生開發工具的版本、官方下載 URL 與 SHA256 記錄於 `tools.json`；第一方程式使用 clang-format、MSVC 警告錯誤化與靜態分析。
本機、hooks、CI 共用 PowerShell 7.4+ 的 `scripts/check.ps1`。Debug 與 Release 都跑正式元件整合測試及安裝後版本檢查。
內附依賴維持現狀並記錄指紋，後續替換另開變更。

## 取捨

能在現有課程程式上執行真實檢查，避免一次遷移 GUI 與依賴管理。
Windows runner／MSVC 的修訂版仍可能變更；指紋無法補足原始二進位依賴的來源證明。
CI 無法取代 GPU、觸控板與繪圖的手動驗證。專案目前沒有 LLM 或線上服務，相應評測與部署規範留待引入時設計。
