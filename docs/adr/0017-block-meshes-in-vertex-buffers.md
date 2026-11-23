# ADR 0017：方塊世界的分塊 mesh 改用 vertex buffer，建好後放掉建造用的格子

日期：2026-11-23
狀態：採用

## 背景

使用者在筆電上看到程式佔約 500 MB。在 Linux（Mesa）量到：方塊島與海底建好後約 380 MB，建造中最高約 500 MB。其中：

- 島與海底的分塊 display list 共約 265 萬個頂點（島 203 萬、海底 62 萬），每個頂點五個 float 屬性，60 bytes，約 150 MB 放在程式自己的記憶體。
- 島的格子（`Grid`）有 7 層、每層 325×93×325 格各 1 byte，約 69 MB，建好之後一直留著；切到平坦編輯島時再多一份。

## 決策

- 新增 `BlockMesh`：`LandWorld`、`OceanWorld` 共用的頂點格式與 mesh。頂點壓成 44 bytes（位置、材質座標、光照資料維持 float；法線用 signed byte、顏色用 unsigned byte），每個分塊每種用途一個 `GL_STATIC_DRAW` vertex buffer，每種材質一段，用 `glDrawArrays(GL_QUADS)` 依原本 display list 的順序畫。
- 畫完把目前的顏色、法線、材質座標（含 unit 1 的光照資料）設成最後一個頂點的值，和 display list 留下的狀態一樣，之後畫的東西（風車、動物、軌道）看到的狀態不變。
- 材質仍以名稱綁定；換材質包時圖片放進同一個名稱，mesh 不用重建。
- 島建好後只留 `block`（`solidAt()`、木棧橋、動物的地面）與 `biome`，放掉 `plant`、`sky`、`warm`、`cool`、`carved`、`level` 六層（約 59 MB）。GL context 重建時從存檔重讀（沒有存檔就重新生成）再重建 mesh。
- F3 與 `--benchmark` 報告顯示程式的記憶體（`PerfStats::memory()`：Windows 的工作集與 private bytes，Linux 的 RSS），方便比較。

## 取捨

- 顏色 0.85–1 存成 byte，畫面最多差一個色階；軸向法線完全一樣，少數斜向法線（植物）差不到 1/254。Linux 離屏比對：大部分差異是一個色階，遠處樹葉偶有單一像素不同。
- 光照資料有負值與大於 1 的值，維持 float，不壓成半精度。
- Mesa（llvmpipe）的 vertex buffer 仍在程式記憶體裡，所以 Linux 上只量得到 44／60 的縮小與格子的釋放；在有顯示卡的 Windows 上，vertex buffer 放在顯示卡記憶體，程式自己的記憶體應該降得更多，以筆電實測為準。
- 沒有刪除舊 context 的 buffer：context 已經不在了，在新 context 用舊名稱刪除會刪到別的東西（與原本的 display list 相同）。
