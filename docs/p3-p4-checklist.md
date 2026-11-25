# 期末大專題（P3 + P4）開發清單：要做什麼與不能做什麼

本專案是結合 P3（Trains and Roller Coasters）與 P4（Water Surface and Rendering）的**期末大專題**，沒有固定 `Grading.doc`。
兩份課程 PDF 裡列出的分數項視為**「至少要做到」的底線基準**；原則是**沒規定不能做就可以做，鼓勵盡情發揮想像力**。

---

## 現況速查（已達標底線，後續改動不可退步）

### P3 已完成
- **基本要求**：OpenGL 視窗與多視角（World / Top / Train / Chase / Fly）、地面與場景、控制點編輯（Gizmo / 快捷鍵 / Undo-Redo）與 `.txt` 存取、Cubic B-Spline（含 Linear、Cardinal）閉合軌道、火車動畫與速度控制、Train 第一人稱視角。
- **進階功能**：
  - Arc-Length Parameterization：列車等速行駛可開關對比，枕木依弧長表等距排列。
  - 立體平行雙軌 ＋ 等距木枕木（VBO 繪製）。
  - 多節車廂：1 節熔爐車頭 ＋ 5 節礦車，依弧長維持間距並附連結桿。
- **既有特色**：自製方塊風 UI、62 點 `Grand Tour`、依坡度與轉彎動作且下坡會舉手的乘客、Shader 局部車頭燈、大型多生態非平坦島嶼與海底世界、自動木棧橋、沿軌道挖空的礦坑／水晶隧道、日夜循環、雙層 Shadow Map、SSAO、Bloom / God rays、下雨濕地反射、材質包熱切換、F3 效能畫面。

### P4 已完成
- **Sine waves**：8 組 Gerstner Waves（Vertex Shader 推動 2D 網格）＋ 3 層細節噪點波紋與雨滴漣漪（Fragment Shader）。
- **Multi-passed scenic refraction and reflection**：獨立 Reflection Pass（鏡射相機＋裁切面）＋ 場景折射與深度複本 ＋ Planar Reflection 與 SSR 混合 ＋ Beer-Lambert 水深吸收 ＋ Schlick Fresnel。
- **Caustics effect**：動態雙層 Voronoi 焦散網格投射於海底與水下場景。

---

## 要做什麼（To Do）

### 1. 至少要做到的底線（補齊 P4 與 P3 題目基準項）
- [ ] **Wave equation 水面模擬（P4 基準）**
  - 用 GPU Shader（Ping-Pong FBO）或 2D 網格數值求解波動方程式，並支援互動激起波浪（例如滑鼠點擊水面、雨滴落點，或列車進出海面處）。
- [ ] **Height maps 水面生成（P4 基準）**
  - 載入課程提供的 `HeightMap` 序列圖（或多層高度圖）做 Vertex 高度位移與法線計算。
- [ ] **水面模型與反射／折射展示切換（P4 基準）**
  - 在選單提供 `Water Model`（`Gerstner` / `Height Map` / `Wave Equation`）及反射／折射對照（純天空環境反射折射 vs Multi-pass 場景反射折射）即時切換；預設維持目前視覺最佳的設定。
- [ ] **整合水下光學強化分支**
  - 檢查並整合 `claude/project-thread-4ey6fb` 的真實 Snell 窗、焦散圖與水下體積光柱進 `main`。
- [ ] **雲霄飛車簡易物理（P3 基準加分項）**
  - 在 `Ride` 選單新增 `Physics` 開關（搭配 Arc-Length）：依高度差做動能／位能加減速（上坡慢、下坡快），設最低速度避免停在坡頂。
- [ ] **3D 翻轉／垂直迴圈朝向支援（P3 進階項）**
  - 改善 `makeTrackFrame()`（改用平行傳輸框架或納入 `ControlPoint::orient`），讓大迴圈（Vertical Loop）與螺旋軌道不會發生側向翻轉。
- [x] **Adaptive subdivision（P3 進階項）**
  - 依曲率自適應調整軌道取樣密度，附取樣點／線框檢視模式方便 Demo 展示差異。（`Track` > `Adaptive`／`Samples`，見 [ADR 0020](adr/0020-adaptive-track-subdivision.md)）
- [ ] **火車冒煙（P3 創意項）**
  - 在熔爐礦車上方加入上升消散的蒸氣／火星粒子。

### 2. 發揮想像力（沒規定不能做，全數開放自由發揮）
底線補齊後（或開發過程中），可以往最酷、最有記憶點的期末大專題方向自由延伸，例如：
- [ ] **列車與水面深度互動**：列車衝入或躍出海面時，直接觸發 Wave Equation 擴散波浪、飛濺水花粒子與鏡頭水珠特效。
- [ ] **更多過山車與軌道玩法**：多列車／分支軌道與道岔、快速插入 Loop / Corkscrew / Helix 樣板、手繪軌道、直接操控 C2 曲線、列車動態車輪／避震細節。
- [ ] **進階光影與渲染實驗**：獨立 Ray Tracing 拍照／渲染模式、體積雲／霧、火山噴發與岩漿火星、夜晚煙火秀、更多海底與島上動態生態。
- [ ] **完整成品呈現**：整理專題技術說明、錄製精彩 Demo 展示影片、建立 `Gallery/` 收集高品質截圖與說明。

---

## 不能做什麼（Do Not Do）

原則上**不設限創意方向**；以下只列「會搞壞現有成品、破壞體驗或踩工程紅線」的真正禁區：

### 1. 不要破壞現有成品與體驗
- **不要讓現有海面與光影退步**：新增 `Height Map`、`Wave Equation`、`Sky-only` 等模式必須做成可切換選項，預設畫面不可比目前的 Gerstner ＋ Planar/SSR ＋ 焦散 ＋ 日夜雨景差。
- **不要破壞整體視覺和諧**：發揮創意時維持方塊世界一致的美學與比例，不硬塞格格不入或明顯破圖的素材。
- **不要破壞舊軌道檔相容性**：既有 `.txt` 控制點讀寫格式須維持可讀（`TrackRoundTripCheck` 必須持續通過）。
- **不要讓新景物擋到軌道**：新增地形、水體或建築不可入侵 `Grand Tour` 三種樣條的列車淨空（維持 `LandRideClearanceCheck`、`OceanRideClearanceCheck` 通過）。
- **不要弄壞固定管線回退**：新 Shader 或 FBO 初始化失敗時，程式仍要能安全降級執行，不可直接崩潰。
- **不要塞進沒驗收的壞掉功能**：每個放進展示選單的功能都要能穩定運作、沒有明顯穿模或閃退；實驗中的點子先留在獨立分支，穩定再合進 `main`。

### 2. 不要踩工程與版控紅線（依 `AGENTS.md`）
- **不要把建置產物、執行資料或私密檔案提交進版控**：`build/`、`out/`、`data/`、`backups/`、`logs/`、`.exe`、`.obj`、`.pdb`、材質包 `.zip` 不得入版控。
- **不要把大量未經整理的原始大圖直接灌爆 git 歷史**：使用 `HeightMap` 序列圖等外部資產時，留意解析度、壓縮與執行期載入／安裝路徑規劃。
- **不要擅自修改 `include/`、`lib/` 第三方檔案**：會觸發 `vendor-lock` 檢查失敗；確有需要須註明來源與 ABI 相容性並更新 `vendor-lock.json`。
- **不要忽略警告、格式與驗證閘門**：
  - 編譯警告視為錯誤，不隨意關閉警告或無註解抑制。
  - 修改程式碼後須通過 `clang-format`、MSVC 與 Clang 的 Debug／Release 建置與 CTest。
  - 涉及畫面或輸入的功能必須實際用 `scripts/control-app.ps1` 截圖與手動確認，不只憑「build 過了」就結案。
- **不要改了程式碼卻忘了同步文件**：改架構同步 `docs/architecture.md`，改可見功能同步 `.agents/skills/verify-app/references/features/`，新增測試同步 `CMakeLists.txt`、`tests/linux/CMakeLists.txt` 與 `docs/development.md`。
