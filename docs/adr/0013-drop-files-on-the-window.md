# ADR 0013：把檔案拖進視窗來加入軌道與材質包

日期：2026-11-21
狀態：採用

## 背景

[ADR 0011](0011-in-game-track-list-and-notices.md) 把 Load／Save 改成遊戲內的 `Load Track` 清單，只列出 exe 旁 `TrackFiles\` 的 `.txt`。`Open Folder` 只是用檔案總管打開這個資料夾，在檔案總管點檔案不會讓遊戲載入它；要用別處的軌道，得先複製進資料夾，再關掉、重開 `Load Track` 讓清單重讀。使用者在 Windows 上試過，覺得不夠直接（issue #52）。

方塊遊戲的資源包畫面也只有「開啟資料夾」，但另外有兩件事讓它好用：把檔案拖進遊戲視窗就複製進資源包資料夾並出現在清單裡；清單會跟著資料夾的變化更新。

## 決策

- 照方塊遊戲：GLFW 的 drop callback（`TrainWindow::dropCallback`）把拖進視窗的路徑交給 `GameMenu::dropped()`。
  - `.txt`（不分大小寫）是軌道檔：用 `Folders::copyInto()` 複製進 `TrackFiles\`，打開 `Load Track` 並選好它，按 `Load`（或 Enter、雙擊）就載入。和方塊遊戲一樣只加入清單、不直接套用，因為載入會換掉目前的軌道。
  - 其他的 `.zip` 或資料夾先用 `TexturePack::Pack::open()` 確認是材質包，再複製進 `texturepacks/`（`addTexturePack()`），打開 `World` 並捲到它；點它才套用。
  - 都不是的檔案（或複製失敗）以 `notify()` 列出名稱與原因；同時拖進的其他檔案照常加入。
  - 畫面上顯示 `Added loop.txt`（或 `Added ... and N more`）；沒拖過檔案時顯示 `Drop ... on this window to add them`，讓人知道可以拖。
- `Folders::copyInto()`：從 `TrackFiles\` 本身拖出來的檔案（`Open Folder` 開的那個視窗）不複製，直接選它；同名且內容相同的檔案也不再複製一份；同名但內容不同時保留原檔，複本取名 `loop (2).txt`（資料夾是 `My Pack (2)`），和 Windows 檔案總管的做法相同，不會蓋掉任何檔案，也不必連續問 `Replace ...?`。
- 路徑一律是 UTF-8：`TrackFiles\` 的清單與拖進來的路徑本來就是 UTF-8，但 `CTrack::readPoints()`／`writePoints()` 用 `fopen_s`，Windows 會把名稱當成 ANSI 字碼頁（繁中系統是 Big5），中文檔名的軌道打不開。改用 `openUtf8()`（`Utilities/Utf8File.H`，Windows 上是 `_wfopen_s`），`main()` 也用 `CommandLineToArgvW` 把命令列轉成 UTF-8，所以 `--track`、`--pack` 與截圖路徑有中文也一致。
- 視窗重新取得焦點或滑鼠移回視窗時（`InputType::Focus`／`Enter`），`Load Track`、`Save Track` 與 `World` 重讀資料夾，保留目前選的那一列，所以從檔案總管放進去的檔案切回遊戲就看得到。

## 取捨

- 不開系統的開啟檔案視窗（`IFileOpenDialog`）：延續 ADR 0011 不用系統對話框、照方塊遊戲做的方向。要用別處的檔案就拖進來，或放進資料夾。
- 不監看資料夾（`ReadDirectoryChangesW`）：切回遊戲視窗時重讀已經足夠，而且不必多一條執行緒。
- 像素字型只有 ASCII，中文檔名在清單裡仍顯示成 `?`，但載入與儲存用的是真正的檔名。要顯示中文得另外準備 Unicode 點陣字型（方塊遊戲的做法），不在這次範圍。
- 拖進來的檔案一律複製；不會從原處搬走，也不會記住原本的位置。
- 拖放只在載入畫面結束、場景出現後才收；自動化執行（`--screenshot` 等）不收。
