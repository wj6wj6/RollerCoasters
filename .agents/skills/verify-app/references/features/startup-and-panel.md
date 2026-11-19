# 啟動、控制面板與展示

## 有什麼

- 啟動時先出現載入畫面（深色泥土背景、階段文字、綠色進度條），島與海床在背景執行緒建造；生成好的世界存在 exe 旁的 `data\saves\*.world`，下次啟動直接讀檔。
- 載入完成後進入 `Grand Tour` 展示：`Train` 相機、`Run` 沒有開、列車停在上岸處。
- 右側深灰控制面板，由上到下六個面板：`Ride`、`Camera`、`Track`、`Transform`、`Lighting`、`World`。
- `Reset` 切到平坦草地島、顯示控制點、World 相機；`Grand Tour` 回到展示。

## 怎麼到達

開啟程式即可；`Reset`、`Grand Tour` 在 `Track` 與 `World` 面板。

## 怎麼用 CLI 驅動

- 成功：`smoke -Only tour`（預設展示的 Train 畫面）。輸出的 `world` 行是場景出現所花的秒數，可以拿來比較第一次生成與讀檔。
- 空狀態：`smoke -Only world-flat`（`--scenery off`，接近 `Reset` 後的畫面）。
- 視窗大小：`screenshot -Name narrow -AppArgs '--size','800x600'`。
- `Run`、`Reset` 按鈕本身沒有 CLI，需要手動檢查。

## 容易騙過檢查的假象

- 截圖只有 OpenGL 畫布，看不到控制面板；面板文字被裁切只能手動看。
- 載入畫面也是「畫出東西」的畫面：泥土背景加進度條會通過 `frame` 檢查。所以 `--screenshot` 只計算載入完成後的幀；`world` 行的秒數若接近 0，表示讀到了存檔。
- 讀檔與重新生成走不同的路徑。要測生成，先刪掉 `data\saves\`（或重新建置 `LandWorld.cpp`），否則測到的只是讀檔。
- 場景出現後的第一幀，貼圖可能還沒全部換好，所以預設畫 5 幀。

## 手動檢查

- 啟動後視窗立刻出現深色泥土背景的載入畫面（`Generating terrain` → `Building terrain`，綠色進度條持續前進），不會停在白色或沒有回應的視窗；載入期間可拖動、縮放視窗；記錄 Debug 與 Release 各自到出現場景的秒數。
- 關閉後再開一次：載入畫面顯示 `Loading world`，比第一次快很多，exe 旁出現 `data\saves\island.world` 與 `ocean.world`；修改並重新建置 `LandWorld.cpp` 後再開，會重新生成；刪掉 `data\saves\` 也會重新生成。
- 載入完成後關閉 `Scenery` 會短暫顯示平坦島的載入畫面，再開啟時立刻回到原島（不重新生成）。
- 正常啟動即進入展示：`Train` 相機、`Run` 沒有開、列車停在原地，按 `Run` 才開始行駛；按 `Reset` 後看得到平坦島、軌道與控制點。
- 視窗縮放後繪圖正常；六個面板的文字都沒有被裁切，Spline Type 三項不需捲動。
- `Run`、速度與前後步進操作正常。
- 關閉視窗會結束程序。
