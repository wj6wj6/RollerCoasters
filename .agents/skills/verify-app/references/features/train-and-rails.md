# 列車與軌道外觀

## 有什麼

- 方塊遊戲的礦車列車：燃燒中的熔爐礦車在前，後面五節礦車，每節沿軌道各自轉向；每節坐一位玩家（遊戲裡的比例與坐姿），動作依軌道變化；材質包的玩家外觀、礦車與熔爐都會套用。
- 方塊風的鐵軌與枕木，枕木依弧長等距擺放；三種樣條（Linear、Cardinal、B-spline）。
- 鐵軌依自適應細分取樣（`Track` > `Adaptive`，預設開）：彎道切得細、直線只用幾段；關掉時回到每段固定 48 點。`Track` > `Samples` 在最上層畫出取樣（青色中心線、黃色橫線、紅點），`Track` 畫面與 F3 寫出取樣數與固定取樣的點數。

## 怎麼到達

`Grand Tour` 或任一軌道；World 或 Chase 相機看得最清楚。

## 怎麼用 CLI 驅動

- 成功：`smoke -Only cameras-chase`；看列車細節可用 `screenshot -Name train-close -AppArgs '--camera','chase','--u','30'`。
- 夜晚的車燈：`screenshot -Name train-night -AppArgs '--camera','chase','--time','22'`。
- 枕木間距與快取網格由 `TrackSpacingCheck` 測試；樣條沒有 CLI 參數，要在選單 `Track` 的 `Spline` 切換。
- 自適應細分對照：`screenshot -Name samples-adaptive -AppArgs '--camera','world','--samples','on'` 與 `screenshot -Name samples-fixed -AppArgs '--camera','world','--samples','on','--adaptive','off'`；取樣規則由 `TrackSubdivisionCheck` 測試，`--benchmark` 報告寫出取樣數；`Track` 畫面的開關與點數一行：`screenshot -Name menu-track -AppArgs '--menu','track'`。
- 軌道網格只在控制點移動或樣條改變時重算：Edit 拖曳控制點、`Spline` 切換、Undo／Redo、載入軌道後，鐵軌與枕木要立刻跟著變，不能停在舊形狀。

## 容易騙過檢查的假象

- 直線段上各節礦車不會互相穿插，彎道才看得出來，所以要多換幾個 `--u`。
- 靜止的截圖看不出行駛中的晃動變形。

## 手動檢查

- World 相機下，列車是熔爐礦車加五節礦車，轉彎時各節各自轉向、不互相穿插；礦車是鐵製外框、內側較亮、有鐵地板，熔爐礦車的爐口朝前、夜晚發亮。
- 礦車和鋼軌的比例像方塊遊戲：礦車比兩條鋼軌的外緣略寬、坐在鋼軌上方，不會看起來比軌道窄小；鋼軌是細的鐵條。
- 每節一位玩家，坐在礦車地板上、雙腳往前，大小與方塊世界相符（頭約半格）；手與身體不穿過車壁；平路時雙手向前伸過車緣、偶爾轉頭或揮手，爬坡時往上看，下坡時多數人高舉雙手並後仰（少數人仍扶著車緣），彎道時身體往外側傾，動作隨軌道平順變化，不會突然跳動。套用 Classic Faithful 32x 時每位乘客是不同的玩家外觀（帽子、外套等外層也顯示），舊版材質包用 steve；切回 Built-in 時回到內建的五種外觀。
- 行駛或靜止時，駕駛室與綠衣服乘客都不會晃動變形；夜晚車頭燈與尾燈會發亮。
- `Track` > `Samples: ON`：直線上的黃色橫線稀疏、彎道密集；切 `Adaptive: OFF` 後變成處處一樣密，`Track` 畫面下方的點數變成固定取樣的數字；切回 ON 鐵軌立刻變回自適應，且兩種下鐵軌看起來一樣平順（Chase 相機在急彎處比對）。Linear 樣條下直線段只有轉角兩側的點，鐵軌在轉角不歪斜。
- `Grand Tour` 用三種 spline 各跑一圈：軌道沒有銳角的急彎或髮夾彎（岩山往東北出海、火山口上方的轉彎都是平順的大弧線）。
