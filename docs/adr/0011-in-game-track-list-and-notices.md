# ADR 0011：遊戲內的軌道清單、提示與 stb_image

日期：2026-11-21
狀態：採用

## 背景

[ADR 0010](0010-glfw-window-and-game-menu.md) 之後，程式只剩三處用到 FLTK：Load／Save 與 `Open Pack...` 的檔案視窗（`Fl_Native_File_Chooser`）、錯誤提示與覆蓋確認（`fl_alert`／`fl_ask`），以及材質包的 PNG 解碼（`Fl_PNG_Image`）。使用者要整個換掉 FLTK，介面照方塊遊戲的做法（2026-11-19，project thread）。方塊遊戲不開系統的檔案視窗：世界有自己的清單，資源包只有「開啟資料夾」，訊息與確認都是遊戲內的畫面。

## 決策

- `Track` 的 `Load...`／`Save...` 改成 `GameMenu` 的 `Load Track`、`Save Track` 畫面，列出 exe 旁 `TrackFiles\` 的 `.txt` 檔（`Folders::list()`），像世界清單。`Load Track` 點選後按 `Load`、雙擊或 Enter；`Save Track` 有名稱欄（`Gui::textField()`），存到已有的名稱先問 `Replace ...?`。`Load Track` 的 `Open Folder` 打開 `TrackFiles\`，別處的軌道檔放進去就會列出。
- `GameMenu::notify()`（訊息與 OK）與 `confirm()`（Yes／No）取代 `fl_alert`／`fl_ask`；按完回到原本的畫面。`CTrack::readPoints()`／`writePoints()` 不再自己跳視窗，改傳回錯誤文字，由呼叫的人決定怎麼顯示（選單用 `notify()`，命令列印出來並回 1）。讀檔失敗時軌道不變。
- 材質包拿掉 `Open Pack...`，只留 `Open Pack Folder`，和方塊遊戲相同；放在其他地方的材質包仍可用 `--pack` 啟動時套用。
- `FileDialogs` 換成 `Folders`：列檔、檢查 Windows 能不能用的檔名、補副檔名、建資料夾、用檔案總管開資料夾（`ShellExecuteW`），由 `FoldersCheck` 測試。
- 打字：`GlfwInput::text()` 把 GLFW 的 char callback 轉成 `InputType::Text`；`Gui` 只收可印的 ASCII（像素字型畫得出的字），Backspace 從 `KeyDown` 來。視窗仍關閉輸入法（[ADR 0010](0010-glfw-window-and-game-menu.md)），所以注音開著時英數字照常輸入、不跳選字視窗；軌道名稱不能打中文。
- PNG 解碼改用 stb_image 2.30（public domain 或 MIT 擇一，授權寫在檔尾），放在 `include/stb-2.30/stb_image.h`（官方 repository 的原檔，沒有修改）。實作由 CMake 產生的一個 C 檔編成靜態的 `stb_image` target（`STBI_ONLY_PNG`、`STBI_NO_STDIO`），和 GLAD、GLFW 一樣不套本專案的警告設定；`TexturePack` 只用 `stbi_load_from_memory()`，原本的 PNG 簽章與大小檢查保留。

## 取捨

- 不能再從任意位置挑軌道檔；要先放進 `TrackFiles\`（`Open Folder` 一鍵打開）。命令列的 `--track` 仍可給任何路徑。
- 清單只在打開畫面時讀資料夾，不監看變化。
- 程式碼已不再 include FLTK，但 CMake 仍連結內附的 FLTK（也帶入 OpenGL／GLU 的系統函式庫）與 `vendor-lock`；下一步移除 FLTK 時一起改（[ADR 0012](0012-remove-fltk.md)）。
- 外部函式庫（stb_image）建議先跟助教確認。
