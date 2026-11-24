# ADR 0018：每個方塊世界一個 vertex buffer，每個 pass 依材質合併繪製

日期：2026-11-23
狀態：採用

## 背景

[ADR 0017](0017-block-meshes-in-vertex-buffers.md) 把島與海床的分塊 mesh 放進 vertex buffer 後，每個分塊每種用途各自一個 buffer。畫一個 pass 時，每個看得到的分塊都要綁一次 buffer、設一次所有頂點陣列（`glPushClientAttrib`／`glPopClientAttrib`），再對它用到的每種材質各綁一次材質、各呼叫一次 `glDrawArrays`。Light+ 一幀要畫四到五次（兩層 shadow map、水面倒影、主場景）。

在 Linux（Mesa llvmpipe，1280x720，三個固定視角）數到：主場景一個 pass 的島、海床與水約 3000–4900 次 `glDrawArrays`、3100–5000 次換材質、1000–1800 次換 buffer。使用者筆電用 RTX 5060 時，CPU 約 8.8 ms 一幀，比 GPU（約 8.4 ms）還慢，是 fps 的上限；用 Radeon 860M 時，每個分塊換 buffer 約多花 4 ms CPU。

## 決策

- `BlockMesh::Arena`：一個世界（每座島一個，海床一個）的所有分塊 mesh 放在同一個 arena。arena 由 32 MiB 的 page（vertex buffer）組成，mesh 依序放進最後一個 page，放不下就開新的（比 page 大的 mesh 自己一個）。`Mesh` 記住在哪個 page、每段材質從 page 的第幾個頂點開始。
- 世界建好後 `compact()` 用 `glCopyBufferSubData`（OpenGL 3.1）把所有 page 複製進一個剛好大小的 buffer，最後一個 page 沒用到的空間也還回去；mesh 的位置不用改（每個 page 記住自己在合併後 buffer 的起點）。
- `Arena::draw()` 收下一個 pass 要畫的所有 mesh（仍照原本每個分塊做視錐剔除），頂點陣列只在換 buffer 時設定。mesh 每 `window` 個一組，組內依材質排序，接連的同材質段落綁一次材質、用一次 `glMultiDrawArrays(GL_QUADS)` 畫完。畫完和以前一樣，把目前的顏色、法線、材質座標設成最後一個 mesh 的最後一個頂點。
- 組的大小依顯示卡決定（`drawWindow()`，看 `GL_VENDOR`）：NVIDIA 整個世界一組（`0`），其他顯示卡一個分塊一組（`1`，和改之前同樣的順序）。原因見下面的實測。
- Linux（Mesa，一個分塊一組）同樣三個視角，主場景一個 pass 的換 buffer 從 1000–1800 次降到 12 次，畫面和改之前逐像素相同；整個世界一組時約 110 次 `glMultiDrawArrays`、210 次換材質，畫面也逐像素相同。

## 實測（使用者筆電，Release，1280x720，Light+ 全開，`--benchmark` 600 幀，main／PR 交替）

| 設定 | RTX 5060 Train | RTX 5060 World | Radeon 860M Train | Radeon 860M World |
|---|---|---|---|---|
| main（每分塊一個 buffer） | 8.41 ms (119) | 10.58 (94) | 17.39 (58) | 23.06 (43) |
| 整個世界依材質排序（window 0） | 5.14 (194) | 5.80 (172) | 18.67 | 25.66 |
| window 32 | 5.09 (196) | 5.87 (170) | 18.88 | 24.67 |
| window 4 | 5.91 (170) | 6.77 (148) | 18.16 | 24.34 |
| window 1（一個分塊一組） | 7.01 (143) | 8.46 (118) | 17.44 | 23.35 |

- RTX 被 CPU 卡住：呼叫越少越快，window 0 每幀 CPU 從 8.5 降到 4.3 ms。
- Radeon 860M 被 GPU 卡住：只要把幾個分塊的面依材質排在一起畫，shadow map 與地形的 GPU 時間就多約 1–2.5 ms，組越大越慢；一個分塊一組才回到 main 的水準，同時保留省下的 CPU 時間（4.6 → 2.9 ms）。同一組內不用 multi-draw（逐段 `glDrawArrays`）、不合併 buffer（保留 32 MiB page）都一樣慢，所以原因是繪製順序，不是 multi-draw 或 buffer；推測是相鄰的繪製在畫面上到處跳，深度剔除與快取變差（沒有在驅動內部量）。由近到遠排序分塊對兩張卡都沒有幫助，所以沒有採用。
- 其他顯示卡（Intel 等）沒有實測，比照 Radeon 用一個分塊一組：GPU 端和改之前一樣，CPU 仍省下換 buffer。
- 沒有 OpenGL 1.5 時 page 放在記憶體，沒有 3.1 時不合併（每個 page 一組），沒有 1.4 時逐段 `glDrawArrays`。`plan()` 不需 OpenGL，回傳 `draw()` 會做的呼叫，由 `BlockMeshCheck` 測試。

## 沒有做的

- **材質 atlas／texture array**：合併之後一個 pass 只剩每種材質綁一次（島約 70 次），再省下去的 CPU 很少；而且 Light+ 關掉時走固定管線，不能像 shader 那樣在 atlas 裡重複貼圖或取 texture array 的層，材質包換圖也是靠同一個材質名稱。之後量到材質綁定仍是瓶頸再考慮。
- **太陽沒動就不重畫 shadow map**：shadow map 跟著相機移動（以相機前方為中心），列車與軌道也投影，所以騎乘時每幀都得重畫。要快取得把地形和會動的物體分開畫進兩張圖再合成，另案評估。
- **Greedy meshing**：會減少頂點數（GPU 時間），但和這次減少呼叫次數無關，另案。

## 取捨

- 依廠商分支是根據一台筆電兩張卡的實測；其他顯示卡可能有不同的最佳值。
- NVIDIA 上同一種材質的段落合在一起畫，分塊之間的先後順序變了。不透明方塊與 alpha test 的植物有深度測試，不受影響；水與玻璃先畫深度、再只在最前面的表面混色，也不受影響。筆電兩張卡的截圖與 main 逐像素比對，地形、樹、建築、軌道、支架相同。
- `compact()` 時新舊 buffer 同時存在，島約多佔 90 MB 顯示記憶體一瞬間。
- `Arena` 的解構不刪除 OpenGL 名稱（程式結束時 context 可能已經不在）；`unload()` 在目前的 context 呼叫 `release()`，context 重建時呼叫 `forget()`。
