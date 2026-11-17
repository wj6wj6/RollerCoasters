# ADR 0004：CMake preset 不綁定 Visual Studio 版本

日期：2026-11-17
狀態：採用；修改 ADR 0001

## 背景

`windows` preset 寫死 `Visual Studio 17 2022` generator。只安裝 Visual Studio 2026 等其他版本的人 clone 後無法直接 configure，必須自行另寫 `CMakeUserPresets.json`。

## 決策

- 新增 `vs` preset 作為 IDE 入口：Ninja Multi-Config＋`cl.exe`，`architecture`（x64）與 `toolset`（host=x64）採 `external` 策略，由 Visual Studio／VS Code 依已安裝的版本載入開發者環境，CMake 不指定版本。
- `windows` preset 移除 `generator`，改用 CMake 的一般 generator 搜尋，在 Windows 上即已安裝的最新 Visual Studio；保留 MSBuild，`scripts/check.ps1` 與 CI 的用法不變。
- 仍要求 Windows x64 與 MSVC 相容前端（`CMakeLists.txt` 的檢查不變）。內附 FLTK 是 MSVC 靜態庫，依賴 v14 系列工具組之間的二進位相容。

## 取捨

CI 仍只在 `windows-2022` runner 上驗證 Visual Studio 2022；較新版本的 Visual Studio 由使用者在本機驗證，較新編譯器若出現新警告仍依警告視為錯誤處理。
`vs` preset 不在 CI 中執行；它和 `clang` preset 一樣走 Ninja Multi-Config，差別只在編譯器。
