# ADR 0016：軌道網格快取在 vertex buffer

日期：2026-11-23
狀態：採用

## 背景

效能畫面（[ADR 0015](0015-performance-screen.md)）在使用者筆電（Radeon 860M，Release，1280x720，Light+ 全開）量到 Train 相機約 92 ms 一幀，其中 Track 佔 74–97 ms：`TrackRenderer::draw()` 每次都重建弧長表、逐一算每根枕木與每段鐵軌的框架，再用 `glBegin`／`glEnd` 一個頂點一個頂點送出；Light+ 一幀要畫四到五次（兩層 shadow map、倒影、主場景），每次約 20 ms CPU。另外 `TrainView::drawFrame()` 每幀呼叫 `gladLoadGL()` 重新查所有 OpenGL 函式，約 7 ms。

## 決策

- `TrackMesh` 把枕木（`GL_QUADS`）與兩條鐵軌的四個面（八條等長的 `GL_QUAD_STRIP`）算成世界座標的頂點（位置、法線、貼圖座標），頂點順序與原本逐一畫的順序相同。記住算的時候的控制點位置與樣條種類，每次 `update()` 比對，一樣就不重算。
- `TrackRenderer` 把網格放進一個 `GL_STATIC_DRAW` vertex buffer，網格重算後才重新上傳；每個 pass 用 vertex array 畫同一份。沒有 OpenGL 1.5 時直接從記憶體的陣列畫。GL context 重建時（`releaseResources()`）忘掉 buffer，下次重新建立。
- 枕木原本用 `glMultMatrixf` 擺放，現在在 CPU 上先乘好，模型矩陣不再每根枕木變一次；著色器拿到的 `gl_Vertex`、`gl_Normal`、`gl_MultiTexCoord0` 在世界座標上相同，`gl_MultiTexCoord1` 不用陣列，仍是目前值。
- `gladLoadGL()` 只在新的 GL context（`m_contextValid` 為假）時呼叫一次。

## 取捨

- 比對控制點是每幀 O(點數)，比重算便宜得多；只看位置，因為軌道形狀只取決於位置（`orient` 尚未用在軌道上，將來用到時要一起比對）。
- 枕木頂點在 CPU 先乘框架，浮點捨入和 GPU 乘矩陣略有不同；離屏比對（800x600、五個視角）只有邊緣少數像素差一點，看不出來。
- 沒改成 VAO／core profile：其他景物仍是相容模式的固定管線與 display list，維持一致。
