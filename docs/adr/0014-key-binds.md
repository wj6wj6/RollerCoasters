# ADR 0014：可自訂的按鍵（Controls → Key Binds）

日期：2026-11-23
狀態：採用

## 背景

按鍵原本寫死在好幾個地方：`GameMenu::input()`（快捷欄 `1`–`9`、`P`、`[`／`]`、F1）、`TrainWindow::keyCallback()`（F11）、`EditKeys::forKey()`（編輯指令）、`FlyCam::keyFor()`（飛行鍵）與 `TrainView`（R／Home 回到正前方、Fly 的 F 與 Home）。使用者想自己改快捷鍵。

方塊遊戲的做法是 `Options`→`Controls`→`Key Binds`：每個指令一列，點按鍵後按新的鍵，衝突的鍵顯示成紅色，每列有 `Reset`，下方有 `Reset Keys`，設定存在 `options.txt`。

## 決策

- 新增 `KeyBinds`（`src/KeyBinds.*`，不需視窗，`KeyBindsCheck` 測試）：每個指令有 id（存檔用，如 `fly.forward`）、顯示名稱、分組、可用的相機範圍與兩個按鍵。按鍵是 `Input.H` 的鍵碼加上必須按住的 Ctrl／Shift／Alt。預設值就是原本寫死的鍵。
- 比對規則：
  - 一般指令要修飾鍵完全相符，所以 `Ctrl+Z`（Undo）不會在按 `Ctrl+Shift+Z`（Redo）時觸發；原本 G、R、P、數字鍵等不管 Shift 的地方，現在加了修飾鍵就不算。
  - Fly 相機自己讀的鍵（移動、加速、調速）不管修飾鍵，設定時也不記修飾鍵，按住 Ctrl 加速時 W 照樣前進。
  - 左右 Shift／Ctrl／Alt 視為同一鍵（顯示 `Shift`、`Ctrl`、`Alt`）。
- 衝突只看「同一個相機下都能用」的指令：每個指令標記能用的範圍（World／Top、Train／Chase、Fly）。所以 Train 的 `Look Ahead Again`（R）與編輯的 `Tilt Point`（R）不算衝突，因為 `canEdit()` 在 Train／Chase 不成立。
- Fly 相機下的新增、刪除控制點是獨立的指令（`Add Point in Fly` 預設 `Alt+D`、`Delete Point in Fly` 預設 `Delete`），保留 `EditKeys.h` 原本避開飛行鍵的理由（Shift 是下降、Ctrl 是加速、X 容易誤按）。
- 每個指令最多兩個鍵（方塊遊戲只有一個，但原本 Redo、Fly 上升／下降、回到正前方、調速都有第二個鍵）。原本 Fly 上升／下降的第三個鍵 Page Up／Page Down 因此拿掉。
- 介面：`GameMenu` 的 `Screen::Controls`，從 Esc 選單與標題的 `Options` 都能進。點按鍵後畫面只收鍵盤：
  - 按一般鍵就設定（連同當下按住的修飾鍵）；先按修飾鍵時等下一個鍵，若修飾鍵單獨放開，就把修飾鍵本身設成按鍵。
  - Esc 設成 `None`（方塊遊戲也是這樣），點畫面其他地方取消。
  - 等待中 F11 也被當成新鍵，不切換全螢幕（`TrainWindow::keyCallback` 先問 `GameMenu::pickingKey()`）。
- `TrainWindow` 持有一份 `KeyBinds`，啟動時讀 exe 旁的 `data/keybinds.txt`（`main()` 已切到 exe 資料夾），每次修改後 `keysChanged()` 立刻寫回，並用 `KeyBinds::applyTo()` 把飛行鍵交給 `FlyCam::setKeys()`。檔案每個指令兩行（`id=鍵`、`id.alt=第二個鍵`），讀不懂的行保留預設值，所以以後新增或改名指令不會讓舊檔失效。自動化執行（`--screenshot` 等）不寫檔。
- 左上角提示、快捷欄格子角落的鍵、`Ride`／`Camera`／`Lighting` 畫面的說明文字與狀態列都顯示目前設定的鍵。

## 取捨

- 不支援把滑鼠按鍵設成指令：滑鼠的建造與相機操作仍寫死（左鍵移除、右鍵放置、中鍵選取、拖曳轉視角）。
- Esc（選單、取消）、Enter（確定）與移動控制點時的 X／Y／Z 軸鎖定不能改：它們是對話與編輯過程中的固定操作，不是快捷鍵。
- 視窗高度 720 時清單一次只看得到 8 列，要靠滾輪捲動；與方塊遊戲相同。
- 像素字型只有 ASCII，非英文鍵盤上 GLFW 沒有對應鍵碼的鍵（`GLFW_KEY_WORLD_1` 等）不能設定。
