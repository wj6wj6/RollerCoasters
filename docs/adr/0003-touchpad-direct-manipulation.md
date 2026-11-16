# ADR 0003：以原始觸控板報告同時處理旋轉與縮放

日期：2026-11-16
狀態：採用

## 背景

使用者希望在觸控板上同時旋轉與縮放視角。Windows 會把精準觸控板的雙指手勢判定為捲動（`WM_MOUSEWHEEL`／`WM_MOUSEHWHEEL`）或捏合（Ctrl + `WM_MOUSEWHEEL`）其中一種。改用 Direct Manipulation 後實測（2026-11-16，使用者筆電）也一樣：捏合手勢的位移恆為 0，移動手勢的縮放恆為 1，系統在手勢開始時就鎖定其中一種。FLTK 1.3.5 也不處理 pointer 或手勢訊息。

## 決策

`TouchpadGestures` 以 Raw Input 註冊精準觸控板（HID usage page 0x0D、usage 0x05），用 `HidP_*` 解析每根手指的接觸 ID、tip switch 與 X／Y，支援 parallel 與 hybrid 回報模式。兩指放在板上且未按下按鍵時，兩指中點的位移用來旋轉（Alt／Shift 時平移），兩指距離的比例用來縮放，兩者在同一次更新中送給 `ArcBallCam::touchpadGesture()`；放開後依手勢速度做短暫慣性。
Direct Manipulation 仍在 `DM_POINTERHITTEST` 時接手觸控板 pointer，讓 Windows 不再為同一手勢送出滾輪訊息；原始報告可用時忽略它的輸出，否則以它為備援。兩者都不可用時維持原本的滾輪路徑。只在 World 視角、游標位於畫布內時接手。只使用 Windows SDK（`hid.lib`、COM），不新增第三方依賴。

## 取捨

能在同一個手勢中同時旋轉與縮放。
需要自行解析 HID 報告與實作慣性；CI 只能驗證編譯與相機數學，實際手感只能在有精準觸控板的 Windows 上手動驗證。舊式（非精準）觸控板沒有這類報告，仍走滾輪訊息、無法同時旋轉與縮放。
旋轉靈敏度設定為：一次滑過整個觸控板寬度等於拖曳過整個畫布寬度。
