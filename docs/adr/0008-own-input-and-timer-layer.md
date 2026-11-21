# ADR 0008：相機與畫布改用自己的輸入事件與計時器介面

日期：2026-11-19
狀態：採用（`PlatformFltk.cpp` 已由 `PlatformLoop.cpp` 與 `GlfwInput` 取代，見 [ADR 0010](0010-glfw-window-and-game-menu.md)）

## 背景

使用者決定整個換掉 FLTK，改成像 Minecraft Java 版的做法：GLFW 負責視窗與輸入，介面自己用 OpenGL 畫（課程規定允許其他 UI 工具，例如 glfw）。分成幾個 PR 進行，期間程式每一步都要能照常使用。

FLTK 原本深入到不屬於視窗的程式碼：`ArcBallCam`、`FlyCam` 以 `Fl::event_x()`、`Fl::event_key()` 等全域狀態讀事件，用 `Fl::add_timeout()` 做平滑與按住移動，持有 `Fl_Gl_Window*` 取尺寸與重畫；`TrainView` 的編輯、放點、搬運與 Train 轉頭處理也直接讀 `Fl::event_*()`；`TouchpadGestures` 與載入畫面用 FLTK timeout；測試則改寫 `Fl::e_x`、`Fl::e_keysym` 等全域變數來模擬事件。換視窗系統時這些都得一起改，風險集中在同一個 PR。

## 決策

- `src/Utilities/Input.H` 定義自己的 `InputEvent`（事件種類、滑鼠位置、按鈕、按鍵、修飾鍵、滾輪位移、連點次數）、`Key::`／`Mod::`／`Mouse::` 常數，以及 `Viewport` 介面（畫面寬高、要求重畫）。
- `src/Utilities/Platform.H` 宣告計時器（`addTimeout`、`repeatTimeout`、`removeTimeout`、`hasTimeout`）、`processPending`／`wait` 與 `forgetClick`，語意照 FLTK 的 timeout。
- 這個 PR 先由 `PlatformFltk.cpp` 以 FLTK 實作上述函式，並提供 `platform::fltkEvent()` 把當下的 FLTK 事件轉成 `InputEvent`。`TrainView::handle(int)` 只做轉換，邏輯移到 `TrainView::handleInput()`。
- 按鍵與修飾鍵的數值沿用 FLTK 的數字，`EditKeys`、`FlyCam::keyForScanCode()` 的表不用改寫；`PlatformFltk.cpp` 以 `static_assert` 確保兩邊一致。之後換成 GLFW 時，在轉換函式裡把 GLFW 的鍵碼對應過來即可。
- 測試改用 `tests/TestViewport.h` 的無視窗 `Viewport` 與 `InputEvent` 產生函式，事件經正式的 `handle()`，計時器經正式的 `platform::` 路徑。

## 取捨

- `TrainView` 本身仍是 `Fl_Gl_Window`，`TrainWindow`、`CallBacks`、對話框、`gl_font` 文字與 PNG 解碼仍用 FLTK；它們在之後換成 GLFW 主迴圈與自製介面的 PR 處理。
- `fltkEvent()` 照 FLTK 的習慣把每個欄位都填上（例如按鍵事件也帶滑鼠位置），所以行為和改動前一致；新的視窗層也要提供同樣的欄位。
- 計時器維持 FLTK 的回呼模型，而不是改成每幀更新，這樣這一步不改動任何相機的時間行為；GLFW 版本需要自己的計時器佇列，在主迴圈裡執行。
