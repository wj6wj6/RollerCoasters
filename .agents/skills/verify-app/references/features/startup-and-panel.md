# 啟動、選單、快捷欄與展示

## 有什麼

- 啟動時先出現標題畫面：方塊字的 `RollerCoasters`、`Play`、`Load Track...`、`Options...`（Ride、Camera、Lighting、World 設定）、`Quit`，左下角版本。`Play` 打開 `Select World`：`Grand Tour Island` 與 `Flat Island` 兩列（小圖、名稱、說明、是否已有存檔），點選有白框，雙擊、Enter 或 `Play Selected World` 進入，`Cancel`／Esc 回標題。像方塊遊戲，標題與 `Play` 之前的畫面不載入世界，背景是深色泥土；進入世界時才從存檔讀入（沒有存檔就生成）並顯示載入畫面，`Quit to Title` 後放掉整個世界，`Scenery` 切換時放掉另一個島，記憶體只留正在玩的世界（F3 的 `Mem` 看得到下降）。自動化模式（`--screenshot`、`--doctor` 等）不顯示標題畫面。
- 世界沒建好就進入會出現載入畫面（深色泥土背景、階段文字、綠色進度條）；生成好的世界存在 exe 旁的 `data\saves\*.world`，下次啟動直接讀檔。
- 選 `Grand Tour Island` 進入 `Grand Tour` 展示：`Train` 相機、`Run` 沒有開、列車停在上岸處。
- 全畫面場景，GLFW 視窗；左上角按鍵提示（Esc、`P`、`[ ]`、`1-9`、F1），下方九格快捷欄（`1`–`5` 相機、`6` Edit、`7` 新增點、`8` 刪除點、`9` Run），快捷欄上方是狀態列與剛選的格子名稱。
- F3 切換效能畫面（見 [performance.md](performance.md)），開著時取代左上角按鍵提示；F1 連它一起隱藏。
- F11 切換全螢幕（視窗所在的螢幕、原生解析度），再按一次回到原本的視窗位置與大小（原本最大化的話仍最大化）；`Lighting` 有同一個 `Fullscreen` 開關。
- Esc 選單：`Back to Ride`、`Ride`、`Camera`、`Track`、`Lighting`、`World`、`Grand Tour`、`Quit to Title`；子畫面各有 `Done`，Esc 回上一層。
- `Reset Track`（選單 `Track`）切到平坦草地島、顯示控制點、World 相機；`Grand Tour` 回到展示。

## 怎麼到達

開啟程式即可；按 Esc 打開選單，`Reset Track` 在 `Track`，`Grand Tour` 在主選單與 `World`。

## 怎麼用 CLI 驅動

- 成功：`smoke -Only tour`（預設展示的 Train 畫面）。輸出的 `world` 行是場景出現所花的秒數，可以拿來比較第一次生成與讀檔。
- 空狀態：`smoke -Only world-flat`（`--scenery off`，接近 `Reset` 後的畫面）。
- 視窗大小：`screenshot -Name narrow -AppArgs '--size','800x600'`。
- 選單、快捷欄與 `Run`、`Reset Track` 沒有 CLI，需要手動檢查；自動模式（`--doctor`、`--screenshot`）不畫快捷欄與按鍵提示，截圖和以前一樣只有場景。

## 容易騙過檢查的假象

- 截圖不含快捷欄、按鍵提示與選單；它們的文字、位置與縮放只能手動看。
- 載入畫面也是「畫出東西」的畫面：泥土背景加進度條會通過 `frame` 檢查。所以 `--screenshot` 只計算載入完成後的幀；`world` 行的秒數若接近 0，表示讀到了存檔。
- 讀檔與重新生成走不同的路徑。要測生成，先刪掉 `data\saves\`（或重新建置 `LandWorld.cpp`），否則測到的只是讀檔。
- 場景出現後的第一幀，貼圖可能還沒全部換好，所以預設畫 5 幀。

## 手動檢查

- 啟動後視窗立刻出現深色泥土背景的載入畫面（`Generating terrain` → `Building terrain`，綠色進度條持續前進），不會停在白色或沒有回應的視窗；載入期間可拖動、縮放視窗；記錄 Debug 與 Release 各自到出現場景的秒數。
- 關閉後再開一次：載入畫面顯示 `Loading world`，比第一次快很多，exe 旁出現 `data\saves\island.world` 與 `ocean.world`；修改並重新建置 `LandWorld.cpp` 後再開，會重新生成；刪掉 `data\saves\` 也會重新生成。
- 載入完成後關閉 `Scenery` 會短暫顯示平坦島的載入畫面，再開啟時立刻回到原島（不重新生成）。
- 正常啟動先出現標題畫面，`Play` → `Grand Tour Island` 後進入展示：`Train` 相機、`Run` 沒有開、列車停在原地，按 `Run` 才開始行駛；按 `Reset` 後看得到平坦島、軌道與控制點。
- 左上角提示與快捷欄清楚可讀；縮放視窗、最大化或在高 DPI 螢幕上，介面以整數倍放大、文字不模糊不被裁切；F1 隱藏與顯示。
- 按 `P` 列車開始與停止，提示行跟著變成 `Stop the train`／`Run the train`；`[`、`]` 每次改 0.5，停在 0 與 10。
- `1`–`5` 與點格子切換五種相機，選中的格子有白框，上方短暫顯示名稱；`6` 切換 Edit（格子變綠）；`7` 進入新增點；`8` 刪除選取的點；`9` 與 `P` 相同。
- Esc 打開選單時場景變暗、不再回應 WASD 或拖曳；每個子畫面的按鈕都做一次，確認效果與舊面板相同：`Ride`（Run、Arc Length、Speed、Step Back／Forward）、`Camera`（相機循環、FOV、Fly Speed）、`Track`（Spline 左鍵往後右鍵往前、Edit、Add／Delete、Undo／Redo 依歷史啟用、Load／Save、R±X／R±Z、X／Y／Z 微調、Reset Track）、`Lighting`（五個開關、Time 以 15 分鐘為單位）、`World`（Water、Rain、Scenery、Grand Tour、材質包清單、Open Pack Folder）。
- F11：在標題、選單、各相機（含 Fly 鎖定滑鼠時）都能切換全螢幕；全螢幕時場景與介面填滿整個螢幕、沒有拉伸，介面依新大小重新排版（GUI 縮放可能變大一級）；按住 F11 只切換一次；再按 F11 回到原本位置與大小。先最大化再切換，回來時仍是最大化，按還原鍵回到最大化前的大小。兩個螢幕時，把視窗拖到第二個螢幕再按 F11，全螢幕出現在第二個螢幕。`Lighting` 的 `Fullscreen` 開關與 F11 狀態一致。全螢幕時 Alt+Tab 切到別的視窗，遊戲不縮到工作列、留在後面，別的視窗正常顯示在它前面；點回來（或 Alt+Tab 回來）仍是全螢幕並蓋住工作列。兩個螢幕時點另一個螢幕，遊戲的螢幕照樣顯示。
- Windows 鍵（`Lock Windows Key` 開著時）：遊戲中（在世界裡、沒開選單）按左或右 Windows 鍵，開始功能表不出現、視窗不換到後面（視窗與全螢幕都是），Win+D、Win+Tab 也沒有作用；按 Esc 打開選單、在標題畫面或載入畫面時按 Windows 鍵照常打開開始功能表（全螢幕時遊戲留在後面，不縮小）。遊戲中按住 Windows 鍵再按 Esc 打開選單後放開，開始功能表不會跳出來；選單中按住 Windows 鍵、關掉選單後放開，系統不會以為 Windows 鍵一直按著。Alt+Tab 與 Ctrl+Alt+Del 永遠有效；切到別的程式後 Windows 鍵在那裡照常可用。
- Fly 相機鎖定滑鼠時按 Esc：滑鼠放開並打開選單；按 `G` 搬點、`R` 傾斜或放置新點時按 Esc 只取消該動作，不打開選單。
- 選單 `Quit to Title` 回到標題畫面（泥土背景，世界被放掉），再 `Play` 會重新載入（有存檔時數秒內），標題畫面的 `Quit` 與視窗的關閉鈕都會結束程序。
- 記憶體：進入島後 F3 記下 `Mem`；`Quit to Title` 再進同一個島，`Mem` 回到差不多的數字（不會每次多一份）；`World` 的 `Scenery` 關掉再打開，也不會兩個島同時留著。
- `Flat Island`：平坦草地、起始軌道、World 相機、Edit 開啟（與 `Reset Track` 相同）；改過軌道後 `Quit to Title` 再選 `Grand Tour Island`，自己的軌道仍在，只換成島的地形；選回 `Flat Island` 也一樣。
- 標題畫面按 Esc 不會離開；從標題的 `Options...` 進設定畫面，`Done`／Esc 回 `Options`，再回標題；從標題的 `Load Track...` 載入成功後直接進入世界，`Cancel` 回標題。
- 開著中文輸入法（注音）時，WASD、`P`、數字鍵與 Esc 都照常作用，不會跳出選字視窗。
- `Controls`（Esc 選單與標題的 `Options...` 都有）：
  - 清單依 `Interface`、`Hotbar`、`Ride and Cameras`、`Editing Points`、`Fly Camera` 分組，滾輪（含觸控板兩指）捲動，右側捲軸跟著動；預設沒有紅字，`Reset` 與 `Reset All` 是灰的。
  - 點 `Run / Stop` 的鍵，按鈕變成黃色 `> P <`；按 `K`：變成 `K`，`Reset` 可按，回到遊戲後 `K` 開停列車、`P` 不再有作用，左上角提示變成 `K  Run the train`。
  - 點 `Undo` 的鍵，先按住 Ctrl（顯示 `> Ctrl+... <`）再按 `U`：變成 `Ctrl+U`，Edit 開著時 Ctrl+U 復原、Ctrl+Z 不再復原。只按 Ctrl 再放開：變成 `Ctrl`。等待按鍵時按 Esc：變成 `None`，不會關掉畫面；點畫面其他地方：取消，保留原本的鍵。
  - 把 `Slot 1` 設成 `W`：`Slot 1` 與 Fly 的 `Forward` 都變紅字；`Look Ahead Again`（R）與 `Tilt Point`（R）不是紅字。`Reset All` 後全部回到預設。
  - 把 Fly `Forward` 改成 `I`：Fly 相機下 I 前進、W 不動；按住 Ctrl 時 I 仍會加速前進。把 `Fullscreen` 改成 `F10`：F10 切換全螢幕，F11 不再有作用；設定 `Fullscreen` 的鍵時按 F11 會被設成新鍵，不會切換全螢幕。
  - 清單下方 `Lock Windows Key: ON`（預設）：點一下變 `OFF`，回到遊戲按 Windows 鍵會打開開始功能表；再點回 `ON` 又鎖住。`Reset All` 不改變它；關掉程式再開保持上次的設定（`data\keybinds.txt` 的 `lockWindowsKey=` 行）。
  - 關掉程式再開：改過的鍵仍在（exe 旁 `data\keybinds.txt` 每個指令一行）；刪掉這個檔案再開，全部回到預設。
- 關閉視窗會結束程序。
