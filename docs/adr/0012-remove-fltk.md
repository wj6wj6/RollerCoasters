# ADR 0012：移除 FLTK

日期：2026-11-22
狀態：採用；取代 ADR 0007

## 背景

使用者要整個換掉 FLTK，改成像方塊遊戲那樣的介面（2026-11-19，project thread）。之前幾步依序完成：自己的輸入與計時器（[ADR 0008](0008-own-input-and-timer-layer.md)）、自己畫的元件（[ADR 0009](0009-own-block-game-style-ui.md)）、GLFW 視窗與 Esc 選單（[ADR 0010](0010-glfw-window-and-game-menu.md)），以及遊戲內的軌道清單、提示畫面和 stb_image（[ADR 0011](0011-in-game-track-list-and-notices.md)）。做完這些以後，已經沒有任何原始碼 include FLTK，只剩建置設定還連結它。

## 決策

- 刪掉內附的 FLTK：`include/FL/`、`lib/Debug/`、`lib/Release/` 的 `fltk*.lib`，以及重新編譯 FLTK 用的 `scripts/build-fltk.ps1`。
- `CMakeLists.txt` 的 `fltk` target 改名為 `opengl`，只留下原本由它帶入的東西：`include/`（課程附的 `GL/GL.h`、`GL/GLU.h`）與 `lib/OpenGL32.Lib`、`lib/GlU32.Lib`。
- Linux 檢查不再需要系統的 FLTK：`tests/linux/CMakeLists.txt` 只連 OpenGL 與 GLU，`install-packages.sh` 不再安裝 `libfltk1.3-dev` 與 `fluid`。
- `vendor-lock.json` 依新的 `include/`、`lib/` 重新產生。

## 結果

- 第三方程式碼（GLAD、GLFW、stb_image、GLM）都隨 repository 附上原始碼或標頭，沒有預先編好的第三方 `.lib`。[ADR 0007](0007-fltk-1-3-11.md) 中「必須用 MSVC 19.44 以上連結」的限制也跟著取消。
- repository 少了約 27 MB 的 FLTK 標頭與靜態庫。git 歷史仍保留這些檔案，要找回可以從 ADR 0007 的 commit 取得。
- 介面文字仍是英文。像素字型只有 ASCII，要顯示中文需要另外的字型來源（[ADR 0009](0009-own-block-game-style-ui.md)）。
