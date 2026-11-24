# ADR 0019：遊戲中 Windows 鍵不作用，全螢幕失焦不縮小

日期：2026-11-24
狀態：採用

## 背景

使用者回報按 Windows 鍵，遊戲視窗就被換到後面。開始功能表搶走焦點；全螢幕時更明顯，GLFW 預設在全螢幕視窗失去焦點時把它縮到工作列（`GLFW_AUTO_ICONIFY`），而且全螢幕視窗是最上層（`HWND_TOPMOST`）。玩的時候手指常在左下角（Ctrl、Shift、Space 附近），很容易誤按。

## 決策

- 遊戲中吃掉 Windows 鍵，像許多遊戲一樣：`TrainWindow::playing()` 為真（視窗有焦點、畫面是場景而不是載入畫面、選單沒開、不是 `--screenshot` 等自動化執行）時，`WindowsKeyHook` 掛上 `WH_KEYBOARD_LL`，吃掉左右 Windows 鍵的按下、重複與放開；其他鍵與 Alt+Tab 不受影響，Ctrl+Alt+Del、Win+L 系統本來就攔不到。
- hook 只在需要時存在：它看得到整個系統的每個按鍵，迴圈忙的時候（載入、長的一幀）會拖慢其他程式的打字，所以不是遊戲中就拿掉。迴圈每一輪才決定一次，選單、標題與載入畫面下 Windows 鍵照常可用，想離開遊戲就按 Esc 再按 Windows 鍵。
- 放開跟按下走同一邊（`WindowsKeyGate`，`tests/WindowsKeyCheck.cpp`）：吃掉的按下，它的放開也吃掉（就算中途開了選單），讓系統看到的永遠是成對的按下與放開，不會跳出開始功能表，也不會以為 Windows 鍵一直按著；吃掉的鍵還沒放開時 hook 先不拿掉。
- 全螢幕失焦不縮小：建立視窗時 `GLFW_AUTO_ICONIFY` 設成 false；`glfwSetWindowMonitor()` 之後把視窗從最上層拿下（`HWND_NOTOPMOST`），否則不縮小的全螢幕視窗會蓋住 Alt+Tab 切過去的視窗。在前面時 Windows 會對填滿螢幕的視窗自動把工作列放到後面，所以全螢幕照樣蓋住工作列。解析度本來就是螢幕原生的，失焦時不必換回。

## 取捨

- 方塊遊戲本身不攔 Windows 鍵，全螢幕 Alt+Tab 會縮到工作列（ADR 前 `startup-and-panel.md` 也是這樣寫的）。這裡改成多數遊戲的做法，因為使用者要的是不被誤按換到後面；兩個螢幕時，點另一個螢幕遊戲也留著，比縮小好用。
- 沒有做開關（`Controls` 或 `Options` 裡的「停用 Windows 鍵」）。需要時再加；選單中照常可用已經是離開的方法。
- Windows 鍵在遊戲中不能當成可綁定的按鍵（hook 先吃掉，GLFW 看不到）。
- Ctrl+Esc 也會打開開始功能表，沒有攔：要同時按兩個鍵，不會誤按。
