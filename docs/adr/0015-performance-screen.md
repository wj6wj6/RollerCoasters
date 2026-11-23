# ADR 0015：效能畫面（F3）與 --benchmark

日期：2026-11-23
狀態：採用

## 背景

使用者想做效能檢測。程式只在需要時重畫（列車與水面每秒 30 步），Light+ 開啟時一幀要畫 shadow map（一到兩層）、水面倒影、主場景與好幾道後製，景物（方塊島、海床、軌道、列車）在每個 pass 都畫一次。沒有任何數字說明時間花在哪裡，也沒有可重複的量測方法。

方塊遊戲的做法是 F3 除錯畫面：左上角 fps 與各種狀態、可開的幀時間圖與 profiler 圓餅圖。

## 決策

- `PerfStats`（不需 OpenGL，`PerfStatsCheck` 測試）把一幀分成依序的**階段**（Setup、Shadow maps、Reflection、Scene、Post effects、Overlay、Interface、Swap）與跨階段累加的**景物**（Land、Ocean、Track、Supports、Train、Water & glass）；Update（列車前進與 timer）在幀之間，算進下一幀。程式碼用 `PerfScope` 標出範圍，透過全域的 `PerfStats::active()`，所以 `renderAdvancedFrame()` 這種自由函式不用多傳參數。
- CPU 用 steady clock。GPU 用 `glQueryCounter(GL_TIMESTAMP)`（`PerfGpu`）：timestamp 可以任意巢狀（`GL_TIME_ELAPSED` 不行），每個範圍的開始與結束各一個；四組 query 輪流，三幀後結果已好才讀，沒好就放棄那一幀的 GPU 時間，不讓 CPU 等。結果依打點順序配對回該幀。
- 一幀的**成本**是 CPU 工作（到 Swap 之前）與 GPU 階段總和中較慢的一個；`Swap / wait` 含等 vsync，不算成本。fps 是實際畫的幀數，另外顯示成本換算的可達 fps。
- 只有 F3 開著或 `--benchmark` 時啟用，關閉時不做任何事。F3 開著時迴圈至少每 0.25 秒重畫，數字才會更新。
- F3 是 `KeyBinds` 的 `Performance Screen`（`ui.performance`），與 F1 同在 `Interface` 組；開著時取代左上角按鍵提示，F1 連同它一起隱藏（同方塊遊戲）。
- `--benchmark <report.txt>`：列車行駛、`glfwSwapInterval(0)`，暖機 30 幀後量 300 幀（`--frames`），寫出平均／最小／95%／最大、各階段與景物的 CPU、GPU 毫秒與前五大成本。`scripts/control-app.ps1 benchmark` 包裝它。

## 取捨

- 不計 draw call 與三角形數：要包住每個 `glDraw*`／`glBegin`，太侵入；階段與景物的時間已足以指出熱點。
- 沒有方塊遊戲 F3 的 Shift／Alt 組合鍵與可展開的 profiler；一個開關看全部。
- `PerfStats::active()` 是全域狀態；程式只有一個視圖，測試裡自己設定與清除。
- GPU timestamp 量的是 GPU 執行到該點的時間，驅動合併或重排命令時個別階段可能有誤差，總和仍可信。
