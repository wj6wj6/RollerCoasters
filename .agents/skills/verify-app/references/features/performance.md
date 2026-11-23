# 效能畫面（F3）與 --benchmark

## 有什麼

- F3（`Controls` 的 `Performance Screen`，可改鍵）切換效能畫面，像方塊遊戲的 F3 除錯畫面，灰底白字蓋在場景上，取代左上角按鍵提示：
  - 左上：版本、最近一秒畫了幾幀（`T: vsync, train 30/s` 說明節奏：列車與水面每秒 30 步，畫面只在需要時重畫）、`Frame cost`（CPU 與 GPU 較慢者，不含等螢幕；60 fps 內綠、30 fps 內黃、更慢紅）與可達 fps、CPU／GPU 毫秒、最小／平均／最大、視窗大小、相機、Light+ 與開著的效果、世界與列車狀態、顯示卡與 OpenGL 版本。
  - 右上：每個階段（Update、Setup、Shadow maps、Reflection、Scene、Post effects、Overlay、Interface、Swap / wait）與每種景物（Land、Ocean、Track、Supports、Train、Water & glass，跨所有 pass 加總）的 CPU 與 GPU 毫秒，右邊比例條是佔一幀成本的比例。
  - 左下：最近幀的成本長條圖，白線是 60 與 30 FPS。
- 開著時至少每 0.25 秒重畫一次，數字才會更新；關閉時不計時（零成本）。
- `RollerCoasters.exe --benchmark <report.txt>`：列車行駛、不等 vsync，先畫 30 幀暖機，再量 300 幀（`--frames` 可改，最多 600），把報告寫到檔案並印出；可配合所有場景參數（`--camera`、`--light`、`--water`、`--size`…）。

## 怎麼到達

進入世界後按 F3；Esc 選單開著時不顯示。

## 怎麼用 CLI 驅動

- `pwsh -File scripts/control-app.ps1 benchmark`：預設情境（Train、Light+ 全開）；`-AppArgs` 加場景參數，例如 `-AppArgs '--camera','world','--light','off'`。報告在 `out/verify/<Name>.txt`。
- 比較前後時固定 `--size`，因為成本和像素數成正比。

## 容易騙過檢查的假象

- F3 的 fps 是「實際畫了幾幀」，不是最快能跑幾幀：列車靜止又沒開 Light+ 時，畫面只有重畫提示時才畫，fps 很低是正常的。要看能力看 `Frame cost` 與 `could run`。
- `Swap / wait` 包含等 vsync 的時間，不算成本；`--benchmark` 關掉 vsync。
- GPU 時間要 OpenGL 3.3 的 timestamp query；沒有時 GPU 欄顯示 `-`，`Frame cost` 只剩 CPU。GPU 時間晚三幀才回來。
- 景物時間（Land 等）巢狀在階段裡，加總會超過一幀，不要和階段相加。
- Debug 組態的 CPU 時間比 Release 慢很多倍，量效能用 Release。

## 手動檢查

- 進入世界按 F3：左上、右上、左下三塊出現，不擋住快捷欄；再按 F3 消失，左上角按鍵提示回來；F1 隱藏全部。
- 開 Light+、列車行駛：fps 接近 30 或更高，右上 Shadow maps、Scene、Post effects 有 CPU 與 GPU 數字；關 Light+：Shadow maps、Reflection、Post effects 變 0。
- 拖曳視角時圖表長條變密；全螢幕（F11）後 `Window` 跟著變，成本變大。
- 在 `Controls` 把 `Performance Screen` 改成別的鍵，新鍵有效、F3 失效。
- 記錄顯示卡、驅動、視窗大小，以及 `benchmark` 報告中的前五大成本。
