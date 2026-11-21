# 軌道檔（Load／Save）

## 有什麼

- 選單 `Track` 的 `Load...`、`Save...` 打開遊戲內的 `Load Track`、`Save Track` 畫面，像方塊遊戲的世界清單一樣列出 exe 旁 `TrackFiles\` 裡的 `.txt` 檔（依名稱排序，不分大小寫）；不開系統的檔案視窗。
- `Load Track`：點一列再按 `Load`，或雙擊、Enter；`Open Folder` 用檔案總管打開 `TrackFiles\`；`Cancel` 或 Esc 回 `Track`。
- `Save Track`：名稱欄（自動補 `.txt`，不能用 `\ / : * ? " < > |`、結尾的點或空白與 CON 等裝置名稱）；點清單裡的檔案會帶入它的名稱；`Save` 或 Enter 存檔，同名時先問 `Replace ...?`。
- 載入失敗時顯示 `Could not load ...` 與原因，軌道不變；成功時選單關閉，快捷欄上方短暫顯示 `Loaded ...`／`Saved ...`。
- 格式：第一行是控制點數（4–65535），之後每行是 `x y z [ox oy oz]`；載入後列車回到起點。

## 怎麼到達

Esc → `Track` → `Load...`／`Save...`。

## 怎麼用 CLI 驅動

- 成功：`smoke -Only track-sample`（`--track` 載入 6 點的範例軌道）。
- 錯誤：`smoke -Only track-bad-count`（只有 3 點）與 `track-missing`（檔案不存在），都必須回 1 並說明原因，不跳出任何視窗。
- 持久化：`TrackRoundTripCheck` 以正式的 `writePoints`／`readPoints` 做往返，`FoldersCheck` 測清單與檔名；畫面上的 Load／Save 只能手動檢查。

## 容易騙過檢查的假象

- GUI 的 `Load`（`readPoints()`）與 CLI 的 `checkTrackFile()` 對點數不合法、行數少於宣告、某行缺 x y z 的檔案都會拒絕；GUI 顯示原因並保留原軌道，CLI 回 1。數字以外的文字（例如 `a b c`）兩者都仍讀成 0，不算錯誤。
- 用唯一的真實軌道測試會有資料遺失的風險；一律先依 `docs/operations.md`「軌道備份與還原」做備份。
- 從 Visual Studio 執行與直接雙擊 exe 的工作資料夾不同，`TrackFiles\` 的位置要兩種都試。
- 清單在每次打開畫面時才重讀資料夾；程式開著時放進去的檔案要關掉再打開 `Load Track` 才會出現。

## 手動檢查

- 用測試軌道的複本執行 Save 再 Load：點數、位置與方向一致；Save 後 `TrackFiles\` 出現同名 `.txt`。
- `Save Track` 輸入 `loop v1.2`：存成 `loop v1.2.txt`；輸入 `a:b` 或 `con`：`Save` 變灰並顯示原因；Backspace 可按住連續刪除。
- 存到已存在的名稱：先出現 `Replace ...?`，按 `No` 或 Esc 回到 `Save Track` 且檔案不變，按 `Yes` 才覆蓋。
- Load 一個宣告點數比實際行數多的檔案（例如第一行寫 5、下面只有 2 行）：顯示 `Could not load ...` 與 `The file says 5 points but has 2`，按 OK 回到清單，軌道與列車位置維持原狀。
- 開著中文輸入法時打名稱：英數字照常輸入，不會跳出選字視窗。
- `Open Folder` 開的是 exe 旁的 `TrackFiles` 資料夾（路徑有中文或空白也能開）；從 Visual Studio 執行與直接雙擊 exe 各試一次。
