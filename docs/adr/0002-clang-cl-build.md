# ADR 0002：加入 clang-cl 編譯

日期：2026-11-02
狀態：採用；擴充 ADR 0001

## 背景

需要使用 Clang 編譯現有 C++ 程式，同時保留內附 FLTK 的 Windows x64 `.lib`。

## 決策

採用 clang-cl 的 MSVC 相容 ABI、Visual Studio C++／Windows SDK 與 Ninja Multi-Config。
新增 `clang` preset，將建置與封裝目錄和 MSVC 分開。共用 PowerShell 入口以 `-Compiler clang` 選擇編譯器；CI 驗證兩個編譯器的 Debug／Release。
clang-cl 與 Ninja 的版本、官方下載及 SHA256 記錄於 `tools.json`，不引入其他腳本語言。
修正 include 大小寫、初始化列表順序、無效 pragma 與未使用的程式碼，維持第一方警告視為錯誤。

## 取捨

Clang 編譯仍需 Visual Studio 的 C++ 工具與 Windows SDK，未涵蓋 Linux、macOS、MinGW 或 ARM。
靜態分析維持獨立 MSVC `/analyze` 共用檢查；Clang 建置不接收 MSVC 專用分析選項。
測試保留正式 FLTK 與軌道讀寫路徑；GPU 與實際輸入裝置仍需人工驗證。
