# 軌道檔（Load／Save）

## 有什麼

- `Load`／`Save` 開 Windows 檔案總管的「開啟」／「另存新檔」視窗，預設在 exe 旁的 `TrackFiles\`。
- 格式：第一行是控制點數（4–65535），之後每行是 `x y z [ox oy oz]`；載入後列車回到起點。

## 怎麼到達

選單 `Track` 的 `Load`、`Save`。

## 怎麼用 CLI 驅動

- 成功：`smoke -Only track-sample`（`--track` 載入 6 點的範例軌道）。
- 錯誤：`smoke -Only track-bad-count`（只有 3 點）與 `track-missing`（檔案不存在），都必須回 1 並說明原因，不跳出對話框。
- 持久化：`TrackRoundTripCheck` 以正式的 `writePoints`／`readPoints` 做往返；實際的 Save 對話框只能手動檢查。

## 容易騙過檢查的假象

- GUI 的 `Load`（`readPoints()`）與 CLI 的 `checkTrackFile()` 對點數不合法、行數少於宣告、某行缺 x y z 的檔案都會拒絕；GUI 跳出錯誤對話框並保留原軌道，CLI 回 1。數字以外的文字（例如 `a b c`）兩者都仍讀成 0，不算錯誤。
- 用唯一的真實軌道測試會有資料遺失的風險；一律先依 `docs/operations.md`「軌道備份與還原」做備份。
- 從 Visual Studio 執行與直接雙擊 exe 的工作資料夾不同，`TrackFiles\` 的位置要兩種都試。

## 手動檢查

- 用測試軌道的複本執行 Load／Save，再載入新檔：點數、位置與方向一致。
- Load 與 Save 開的是 Windows 檔案總管的視窗（不是 FLTK 的灰色檔案選擇器）；Save 輸入沒有副檔名的名稱時存成 `.txt`，選既有檔案時先詢問是否覆蓋；按取消時軌道不變。
- Load 一個宣告點數比實際行數多的檔案（例如第一行寫 5、下面只有 2 行）：跳出錯誤訊息，按確定後軌道與列車位置維持原狀。
- 從 Visual Studio 執行與直接雙擊 exe 各試一次：Save 對話框預設的 `TrackFiles\` 能直接存檔，檔案出現在 exe 旁的 `TrackFiles` 資料夾，Load 視窗也開在那裡。
