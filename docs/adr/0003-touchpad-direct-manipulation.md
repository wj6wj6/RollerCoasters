# ADR 0003：以 Direct Manipulation 處理觸控板手勢

日期：2026-11-16
狀態：採用

## 背景

使用者希望在觸控板上同時旋轉與縮放視角。Windows 會把精準觸控板的雙指手勢轉成捲動（`WM_MOUSEWHEEL`／`WM_MOUSEHWHEEL`）或捏合（Ctrl + `WM_MOUSEWHEEL`）其中一種，同一個手勢不會兩者都有，因此只靠滾輪訊息無法做到。FLTK 1.3.5 也不處理 pointer 或手勢訊息。

## 決策

新增 `TouchpadGestures`，以 Windows Direct Manipulation（與瀏覽器處理觸控板捏合相同的系統 API）接手觸控板手勢：在畫布視窗收到 `DM_POINTERHITTEST` 時對觸控板 pointer 呼叫 `SetContact`，每影格以 FLTK timeout 呼叫 `Update`，再從內容轉換矩陣取出游標下內容的位移與縮放比例，交給 `ArcBallCam::touchpadGesture()` 同時旋轉（或平移）與縮放。每個手勢結束後把轉換重設到內容中央。
只在 World 視角接手；其他視角或建立失敗時維持原本的滾輪路徑。只使用 Windows SDK 與系統元件，不新增第三方依賴。

## 取捨

能在同一個手勢中同時旋轉與縮放，並取得系統的平移慣性。
Direct Manipulation 需要 COM 與視窗 subclass，CI 只能驗證編譯與相機數學，實際手感只能在有精準觸控板的 Windows 上手動驗證。舊式（非精準）觸控板驅動不會送出 `DM_POINTERHITTEST`，仍走滾輪訊息、無法同時旋轉與縮放。
捏合中心假設為游標位置；若某些驅動不是如此，純捏合可能伴隨輕微旋轉。
