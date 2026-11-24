# ADR 0019：遊戲中鎖住 Windows 鍵（可關），全螢幕失焦不縮小

日期：2026-11-24
狀態：採用

## 背景

使用者回報按 Windows 鍵，遊戲視窗就被換到後面。開始功能表搶走焦點；全螢幕時更明顯，GLFW 預設在全螢幕視窗失去焦點時把它縮到工作列（`GLFW_AUTO_ICONIFY`），而且全螢幕視窗是最上層（`HWND_TOPMOST`）。玩的時候手指常在左下角（Ctrl、Shift、Space 附近），很容易誤按。

## 決策

- 遊戲中吃掉 Windows 鍵，像許多遊戲一樣：`TrainWindow::locksWindowsKey()` 為真（`Lock Windows Key` 開著、視窗有焦點、畫面是場景而不是載入畫面、選單沒開、不是 `--screenshot` 等自動化執行）時，`WindowsKeyHook` 掛上 `WH_KEYBOARD_LL`，吃掉左右 Windows 鍵的按下、重複與放開；其他鍵與 Alt+Tab 不受影響，Ctrl+Alt+Del、Win+L 系統本來就攔不到。
- hook 在自己的執行緒：low-level hook 由掛上它的執行緒在等訊息時呼叫，主迴圈忙的時候（長的一幀、建世界）回得慢，Windows 會先把按鍵放過去，逾時幾次後還會默默拿掉 hook、不再呼叫。所以第一次遊戲中（開關開著）時開一條只等訊息的執行緒掛 hook，之後一直留著；是不是遊戲中由迴圈每一輪寫進 atomic，hook 讀它並看前景視窗。開關從沒打開過時不掛。選單、標題與載入畫面下 Windows 鍵照常可用，想離開遊戲就按 Esc 再按 Windows 鍵。
- 放開跟按下走同一邊（`WindowsKeyGate`，`tests/WindowsKeyCheck.cpp`）：吃掉的按下，它的放開也吃掉（就算中途開了選單），讓系統看到的永遠是成對的按下與放開，不會跳出開始功能表，也不會以為 Windows 鍵一直按著；吃掉的鍵還沒放開時 hook 先不拿掉。
- 全螢幕改成無邊框視窗：不再用 `glfwSetWindowMonitor()`（GLFW 的全螢幕是最上層，失焦時縮小），改成關掉邊框（`GLFW_DECORATED`），用 `SetWindowPos` 蓋滿視窗所在螢幕（`MonitorFromWindow`），不是最上層，所以 Alt+Tab、開始功能表、點另一個螢幕都只是蓋住它。視窗比螢幕高 1 像素：剛好填滿螢幕時，顯示卡驅動仍把它當成獨佔全螢幕直接輸出，開始功能表一蓋上來要切回系統合成，畫面會黑一下（使用者的 RTX 筆電上比較過：`glfwSetWindowMonitor` 不論是否最上層都黑，高 1 像素的無邊框不黑；使用者的方塊遊戲也不黑）。在前面時 Windows 會對蓋滿螢幕的視窗把工作列放到後面，所以照樣蓋住工作列。最底下 1 列像素在螢幕外，看不到。

- 開關：`Controls` 畫面清單下方的 `Lock Windows Key`（預設 ON）。方塊遊戲沒有這個選項，但它的玩家找鍵盤設定會到 `Controls`；存在同一個 `data/keybinds.txt`（`lockWindowsKey=true`），`Reset All` 只重設按鍵、不動它。關掉時 Windows 鍵照常（hook 若已掛上就只是全部放行）。

## 取捨

- 方塊遊戲本身不攔 Windows 鍵。全螢幕切走時，使用者的方塊遊戲是被別的視窗蓋住、不縮小（之前 `startup-and-panel.md` 寫成會縮到工作列，是誤寫），這裡改成同樣的行為；兩個螢幕時，點另一個螢幕遊戲也留著。
- 選單中照常可用 Windows 鍵，是不關開關也能離開的方法。
- 無邊框全螢幕和使用者的方塊遊戲（SDL 的桌面全螢幕）同一類：不換解析度、切走不縮小、開始功能表蓋上不黑。代價是不再走驅動的獨佔全螢幕，理論上延遲可能多一點；F3 的幀時間照樣看得到。
- Windows 鍵在遊戲中不能當成可綁定的按鍵（hook 先吃掉，GLFW 看不到）。
- Ctrl+Esc 也會打開開始功能表，沒有攔：要同時按兩個鍵，不會誤按。
