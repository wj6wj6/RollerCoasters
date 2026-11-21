# ADR 0010：GLFW 視窗、Esc 選單與快捷欄

日期：2026-11-21
狀態：採用

## 背景

[ADR 0008](0008-own-input-and-timer-layer.md) 把相機與編輯改成只吃自己的 `InputEvent` 與 `platform::` timer，[ADR 0009](0009-own-block-game-style-ui.md) 加入自己畫的方塊遊戲風元件。剩下的 FLTK 是視窗本身（`Fl_Gl_Window`、`Fl_Double_Window`）、右側控制面板與事件迴圈。使用者選定的版面是「全畫面＋Esc 選單＋快捷欄」（2026-11-19，project thread）：畫面全給場景，Esc 打開設定畫面，下方快捷欄切換相機與編輯工具，F1 隱藏介面，左上角提示主要按鍵，讓助教不用找就知道怎麼開車與調速度。

## 決策

- 視窗與輸入改用 GLFW 3.4（zlib/libpng 授權），原始碼放在 `include/glfw-3.4/`（官方 `glfw-3.4.zip` 的 `include/`、`src/`、`LICENSE.md`、`README.md`，刪掉不用的 `src/CMakeLists.txt`、`glfw.rc.in`、`mappings.h.in`），由 `CMakeLists.txt` 編成靜態的 `glfw` target（只編 Win32 後端，不套本專案的警告設定）。和 GLAD 一樣隨 repository 附上原始碼，不需要另外下載或預先編好的 `.lib`，MSVC 與 clang-cl 都能直接編。
- `TrainWindow` 開一個 OpenGL 相容 context 的視窗（深度 24、stencil 8，依螢幕 DPI 放大），自己跑主迴圈：等事件或下一個 timer，30 Hz 推進列車與光影動畫，只在需要時重畫。`PlatformLoop.cpp` 取代 `PlatformFltk.cpp`，實作同樣的 `platform::` 函式，事件交給 GLFW 的 pump。
- `GlfwInput` 把 GLFW 的 callback 轉成 `InputEvent`。GLFW 的按鍵碼是實體位置，所以不再需要 `FlyCam::keyForScanCode()`；視窗的輸入法一律關閉（沒有文字輸入欄）。滾輪訊息仍先由視窗程序 subclass 交給 `TrainView::systemWheel()`，保留原始位移與觸控板捏合的 Ctrl（[ADR 0003](0003-touchpad-direct-manipulation.md)）。
- `GameMenu` 以 `Ui/Gui` 畫出全部介面：按鍵提示、九格快捷欄（`1`–`5` 相機、`6` Edit、`7` 新增點、`8` 刪除點、`9` Run）、快捷欄上方的狀態列，以及 Esc 選單的 `Ride`、`Camera`、`Track`、`Lighting`、`World` 畫面。每個控制項只讀寫 `SceneSettings` 或呼叫 `CallBacks`；舊面板的每個功能都有對應的位置。事件先給 `GameMenu`，沒用到才給 `TrainView`；Esc 在移動、傾斜或放置控制點時先取消那個動作，和方塊遊戲一樣，Fly 鎖定滑鼠時 Esc 放開滑鼠並打開選單。

## 取捨

- 檔案視窗（`Fl_Native_File_Chooser`）、`fl_alert`／`fl_ask` 與材質包的 PNG 解碼暫時仍用 FLTK，所以仍連結內附的 FLTK。下一步改成遊戲內的軌道清單與確認畫面、以 stb_image 解 PNG（[ADR 0011](0011-in-game-track-list-and-notices.md)），之後才完全移除 FLTK。
- 課程規定列出的其他 UI 工具包含 glfw；外部函式庫仍建議先跟助教確認。
- Linux 檢查不編 `TrainWindow.cpp`（需要 Win32 視窗程序），只做 MinGW 語法檢查；`GlfwInput` 的轉換由 `GlfwInputCheck` 測試，`GameMenu` 在 Linux 上只編譯，元件本身由 `GuiCheck` 測試，選單的畫面與操作要在 Windows 上確認。
