# 光影、時間與天氣

## 有什麼

- `Light+`：shader 逐像素光照、跟著鏡頭的 shadow map；關閉時回到固定管線。
- `Shadows`、`Headlight`、`Water`（海面波浪、反射與折射）、`Bloom`（含 god rays）、`SSAO` 可以個別開關。
- `Time`（0–24 時）控制太陽、月亮與天空；光源白天像裝飾，夜晚與暗處才照亮四周。
- `Rain`：天空轉陰、雨絲、水面漣漪，地面逐漸變濕並出現會反光的水窪。
- `Waves`（`World` 畫面，或 `V`）：海面的 `Gerstner`（預設）、`Height Map`（課程高度圖序列三層疊加）、`Wave Equation`（GPU 解波動方程；`B`、Fly 關 `Edit` 時右鍵丟石頭，列車穿過海面、下雨都會激起漣漪）。

## 怎麼到達

選單（Esc）的 `Lighting` 與 `World`。

## 怎麼用 CLI 驅動

- shader 路徑是否可用：`doctor`。`shader lighting` 或 `post effects` 出現 `FAIL` 時，其他截圖都不能當成光影的證據。
- 成功：`smoke -Only light`（`light-off`、`light-night`、`light-rain`，以及方塊雲的 `light-clouds-dusk`、`light-clouds-night`、`light-clouds-rain-night`）。
- 黃昏：`screenshot -Name dusk -AppArgs '--time','17.75','--camera','world'`。
- 海面波浪模式：`smoke -Only world-waves`（`world-waves-heightmap`、`world-waves-equation`），或 `--waves gerstner|heightmap|equation`。截圖時列車不動，`Wave Equation` 只會是平靜的海面：漣漪要手動看。
- 個別效果：`--shadows on|off`、`--water on|off`；`Bloom`、`SSAO`、`Headlight` 沒有 CLI 參數，要手動切換。

## 容易騙過檢查的假象

- shader 或 frame buffer 建立失敗時，程式會靜默退回固定管線：畫面照樣有東西，只是比較平淡。所以 `--screenshot` 另外回報 `shader lighting` 那一行。
- 雨會在幾秒內慢慢變濕；只畫 5 幀時還看不出效果，所以 `light-rain` 畫 20 幀。截圖仍無法代表完全濕透的樣子，要手動看。
- 陰影閃爍、反射邊緣淡出這類問題要移動相機才看得到，靜態截圖拍不出來。
- `Height Map` 讀不到 `assets/heightmaps/waves.png`（或 exe 旁的 `HeightMap\`）時改用程式產生的替代圖，畫面仍有波浪；stderr 會寫 `WaterSurface: no height maps`，有疑問時看這行，或看 `WaterSurface: 100 height maps of 256 x 256 from ...`。
- 夜晚的方塊雲不再發光，但仍被月光照著：比夜空亮一些是正常的，和月光下的雪山差不多亮；要比的是改之前像白色燈箱的樣子。

## 手動檢查

- `Light+` 開關切換時畫面正常；`Time` 從 0 拖到 24，太陽、陰影、天空與夜晚星空連續變化。
- 約 6–7 時與 17–18 時，陽光與地平線呈金橘色（背向太陽一側偏粉紫），太陽與月亮是方形；21 時到 3 時看得到方形月亮與光暈，場景被偏藍的月光照亮、不會全黑。
- `Shadows`、`Headlight`、`Water`、`Bloom`、`SSAO` 逐一開關，三種相機下都沒有黑畫面或閃爍。
- 用 Fly 在島上與海上飛一圈、快速轉頭：陰影跟著鏡頭，移動時邊緣不閃爍；從海上遠看整座島（例如 `Time` 16:30），遠處的山、樹與房子仍有影子，近處細影與遠處粗影的交界看不出一條線，只有島外很遠處才淡出；記錄幀率。
- 水面近看有起伏的波浪與細碎漣漪，島、樹與軌道的倒影位置正確，靠近畫面邊緣時倒影平順淡出；網格與遠處平面的交界沒有裂縫。
- 從高處看一片開闊的海面：白浪與反光不會排成規律的格子，白浪只在部分區域零星出現；黃昏時夕陽在海面拉出一條不規則閃爍的光帶。
- 方塊雲跟著天色：`Time` 依序調到 5、7、12、17、19、21、24 時（`World` 相機看得到島上空的方塊雲），正午是白色，日出、黃昏染成暖色，21 時到 24 時只剩暗淡的月光藍灰、不像亮著的燈；拖動 `Time` 經過日出與日落時雲的顏色連續變化，沒有一下子跳色。`Rain` 開啟時方塊雲和天空一起轉成灰色。
- 海面波浪模式：`World` →`Waves` 或按 `V` 依序切到 `Height Map`、`Wave Equation` 再回到 `Gerstner`，每次切換畫面不閃黑、海面沒有破洞或接縫。`Height Map` 的波浪大小不一、方向交錯，從高處看不會排成格子，持續播放沒有跳格；`Wave Equation` 預設是平靜的海面。
- 漣漪：`Wave Equation` 下用 World 相機在海面上按 `B`（或 Fly 關掉 `Edit` 後右鍵對著海面），落點出現一圈圈往外擴散的漣漪，幾顆一起丟時會互相干涉，十幾秒內慢慢平息；移動相機時漣漪留在原處。`Grand Tour` 列車衝進海面處有一圈水花、沿著水面跑時拖出尾流；開 `Rain` 時鏡頭附近海面布滿小漣漪。潛到水下抬頭看，水面的扭曲跟著漣漪動。記錄幀率（F3 的 `Water & glass`）。
- `Rain` 開啟約 6 秒內天空轉陰，出現雨絲與水面漣漪，草地出現會反光的水窪，水窪裡有雨滴漣漪；牆面與方塊側面斜看時有柔和的濕潤光澤與往下流的水痕，不是只有頂面反光。
- 屋簷下與樹下較乾，岩山隧道與屋內地面不濕也不反光；草與花不會蒙上灰藍的水膜；關閉 `Rain` 後約半分鐘變乾。
- 關閉 `Light+` 時草與花仍正常顯示（沒有被 alpha test 吃掉），`Rain` 不影響原本的畫面。
- 光源：`Time` 調到正午時路燈只有燈籠本身微亮，周圍地面不偏橘；19 時後與夜晚，燈籠在地面與牆上灑出暖光；岩山隧道裡白天也看得到燈籠壁龕的光；熔岩在夜晚照亮周圍。
