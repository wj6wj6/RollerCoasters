# 啟動、控制面板與展示

## 有什麼

- 啟動即進入 `Grand Tour` 展示：`Train` 相機、`Run` 沒有開、列車停在上岸處。
- 右側深灰控制面板，由上到下六個面板：`Ride`、`Camera`、`Track`、`Transform`、`Lighting`、`World`。
- `Reset` 切到平坦草地島、顯示控制點、World 相機；`Grand Tour` 回到展示。

## 怎麼到達

開啟程式即可；`Reset`、`Grand Tour` 在 `Track` 與 `World` 面板。

## 怎麼用 CLI 驅動

- 成功：`smoke -Only tour`（預設展示的 Train 畫面）。
- 空狀態：`smoke -Only world-flat`（`--scenery off`，接近 `Reset` 後的畫面）。
- 視窗大小：`screenshot -Name narrow -AppArgs '--size','800x600'`。
- `Run`、`Reset` 按鈕本身沒有 CLI，需要手動檢查。

## 容易騙過檢查的假象

- 截圖只有 OpenGL 畫布，看不到控制面板；面板文字被裁切只能手動看。
- 程式啟動時會先建好整座島；只截第一幀時可能還看不到後來才載入的貼圖，所以預設畫 5 幀。

## 手動檢查

- 正常啟動即進入展示：`Train` 相機、`Run` 沒有開、列車停在原地，按 `Run` 才開始行駛；按 `Reset` 後看得到平坦島、軌道與控制點。
- 視窗縮放後繪圖正常；六個面板的文字都沒有被裁切，Spline Type 三項不需捲動。
- `Run`、速度與前後步進操作正常。
- 關閉視窗會結束程序。
