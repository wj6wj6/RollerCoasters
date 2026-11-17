# 發布、備份與驗證

## 發布前

1. MSVC 與 Clang 各自 Debug／Release 的 `scripts/check.ps1 -Stage all` 全部成功。
2. 使用下列 GUI 清單驗證實際 Windows 裝置，於 PR 記錄結果。
3. 備份重要軌道檔並做一次還原驗證。
4. 需要發布新版本時先修改 `CMakeLists.txt` 的 `project(... VERSION ...)`，合併後再建立對應 `v*` tag。

PR、main push、`v*` tag 與手動觸發都執行同一個 Windows CI，tag 發布亦重新檢查。`tools` job 先準備並快取 `tools.json` 指定的工具及檢查格式，四個 `quality` job 再共用快取執行其餘檢查。
CI 會保存測試 JUnit／CTest 日誌，以及已執行 `--version` 的安裝目錄 artifact。
這是桌面應用程式，以 executable 的版本 smoke test 取代服務 healthcheck；沒有 Docker 或伺服器部署。

## 建立可交付目錄

```powershell
pwsh -File scripts/check.ps1 -Stage all -Config Release
./out/package/Release/RollerCoasters.exe --version
```

交付 `out/package/Release/`。目標機器需 Windows x64、支援 OpenGL 的顯示驅動，以及 Microsoft Visual C++ x64 Runtime（2015 之後的 v14 系列，需不舊於建置所用的 Visual Studio 版本）。
使用 Clang 封裝時加上 `-Compiler clang`，交付目錄改為 `out/package/clang/Release/`，執行期前置需求相同。
目前未包含安裝程式、程式簽章與自動更新；正式對外發布前應另行規劃。
`--version` 驗證程式與執行期可啟動，無法驗證 GPU 呈現。

## GUI 驗證清單

- 正常啟動即進入 `Grand Tour` 展示：`Train` 相機、`Run` 沒有開、列車停在原地，按 `Run` 才開始行駛；按 `Reset` 後可見平坦島、軌道與控制點。視窗縮放後繪圖正常；右側深灰控制面板六個面板的文字都沒有被裁切，Spline Type 三項不需捲動。
- 切換 World／Train／Chase／Top／Fly 相機（`Chase` 沿軌道跟在車尾後方，看得到火車頭、五節礦車與乘客，彎道與隧道中不穿進岩壁），Run、速度與前後步進操作正常。
- 雙指旋轉平滑、水平線穩定，手指停下時畫面立即停止；捏合／張開與 Ctrl + 滾輪縮放方向正確且連續。
- 觸控板同一個手勢中邊移動邊捏合，旋轉與縮放同時發生；只捏合時視角不轉動、只移動時不縮放，手指靜止時畫面不抖；放開手指後旋轉有慣性並自然停止。
- 右鍵或空白處左鍵拖曳時用滾輪縮放，畫面不跳動。
- 實體滑鼠滾輪向上放大、向下縮小，每格平滑縮放約 10%、視角方向不變；左右傾斜滾輪平滑旋轉。
- 主控台不再輸出 `[touchpad]` 診斷訊息。
- 在空白處左鍵拖曳可旋轉、點到控制點仍可拖曳控制點；Alt／Shift 平移與重設正常；重設後沒有殘留動畫。
- 分別從 World、Train、Chase 切換到 Fly，畫面都從當下視角接續、不跳動（只有 Train 的傾斜擺正）；從 Top 切換時改為從島中央上方往下看；點畫面空白處後游標隱藏、移動滑鼠（含觸控板單指）直接轉視角且游標不會跑出畫面，Esc 放開滑鼠且視窗不會關閉，切到別的視窗時自動放開；WASD 水平移動、空白鍵／Shift 升降、Ctrl 加速，放開按鍵立即停止；滑鼠滾輪改變速度且 `Camera` 面板的 `Fly` 滑桿同步；切到其他視窗再回來時相機不會自己漂移。
- 開著注音（中文模式）切到 Fly：不會跳出選字視窗，W／A／S／D、空白鍵、Shift 正常飛行；切回 World 後輸入法恢復。
- Fly 模式畫面中央有準星、沒有操作提示文字；低頭按 W 時保持水平；遠離場景時地面延伸到地平線、地面與棋盤格之間沒有閃爍；F 平滑轉向列車，轉向中拖曳可立即接手；按住 Shift 可一路降到 y = −200，從地面下方仍看得到地平線地面與棋盤格。
- Fly 模式下觸控板雙指移動轉向、放開後不再慣性旋轉，捏合／張開前進後退、Alt + 捲動平移（Shift 是下降）；fly speed 滑桿與 +／- 鍵同步；`FOV` 預設 70，拖動時 World、Train、Chase、Fly 的視野都立即改變（Top 不變）；`Home` 回到 World 視角；在 Fly 模式仍可點選並拖曳控制點。
- 用測試軌道複本執行 Load／Save，再載入新檔，點數、位置與方向一致。
- `Light+` 開關切換時畫面正常；`Time` 從 0 拖到 24，太陽、陰影、天空與夜晚星空連續變化。
- World 相機下列車是蒸汽火車頭加五節木頭礦車，轉彎時各節各自轉向、不互相穿插；每節一位乘客，揮手時手不會穿過車壁，火車頭比礦車大，司機與乘客同樣大小、同樣高度，頭和手不穿過屋頂；行駛或靜止時駕駛室與綠衣服乘客都不會晃動變形；夜晚車頭燈與尾燈會發亮。
- 開啟 `Edit` 後才看得到控制點；點選控制點後出現三色箭頭與平面方塊，滑鼠移到把手上會變亮；拖箭頭只沿該軸移動、拖方塊只在該平面移動，`Transform` 面板的數值同步更新，直接輸入數值也會移動控制點。
- `G`、`R` 加 `X`／`Y`／`Z`、`Ctrl` 吸附、Esc／右鍵取消、`Shift` + `D`、`Delete`、`Ctrl` + `Z` 都如 README 所述；World 相機下編輯時不會誤觸發旋轉視角。
- Fly + `Edit`：選取控制點後 W／A／S／D、空白鍵、Shift、Ctrl、E／Q 仍只會飛行；`G` 移動、`R` 傾斜可用，開始時相機停住、游標顯示；期間按 `Shift` + `X` 鎖平面不會讓相機下降；Esc 取消還原，Enter／左鍵確定，之後按住的飛行鍵恢復飛行、放開即停（不會卡住一直移動）。`Alt` + `D` 複製、`Delete` 刪除；`Shift` + `D` 與 `X` 不會複製或刪除；`Home` 回到 World 視角。切回 World 相機後 `Shift` + `D`、`X` 照舊。控制點在地面上沒有影子，左上角文字清楚可讀。
- `Shadows`、`Headlight`、`Water`、`Bloom`、`SSAO` 逐一開關，三種相機下都沒有黑畫面或閃爍；開啟後仍能點選與拖曳控制點。
- 水面近看有起伏的波浪與細碎漣漪，島、樹與軌道的倒影位置正確，靠近畫面邊緣時倒影平順淡出；網格與遠處平面交界沒有裂縫。
- `Rain` 開啟約 6 秒內天空轉陰、出現雨絲與水面漣漪，草地出現會反光的水窪；關閉後約半分鐘變乾。關閉 `Light+` 時不影響原本畫面。
- 按 `Grand Tour` 後用 `Train` 相機跑完一圈的陸地段：村莊風車在轉、窗戶與路燈發亮；雪山平台、峽谷、火山口、岩山隧道都不穿模，
  峽谷裡有海水；溪流與瀑布是半透明的靜態水方塊，往下流的部分是一整格寬的水柱，看得到後面的岩壁，不會捲動；熔岩與燈籠在 `Time` 調到夜晚時照亮周圍；懸空的軌道下有木棧橋且不會立在另一段軌道上；
  動物、雲、熱氣球會動。三種 spline 都試一次；`Scenery` 關閉時回到平坦草地島，再開啟時恢復。
- 同一圈的海底段：入水與出水時畫面切換正常，水下看得到遠處的岩石與遺跡，有光束、微粒、氣泡與各種海洋生物，
  隧道內變暗只剩水晶與頭燈，列車不會穿過方塊，水草與珊瑚不碰到鐵軌；三種 spline 都試一次。
- 關閉視窗可結束程序；記錄 Windows、GPU／驅動與輸入裝置。

## 軌道備份與還原

Load／Save 會直接讀寫指定檔案。儲存前保留原檔，尤其不要用唯一的資料副本測試新版本。

```powershell
New-Item -ItemType Directory -Force backups | Out-Null
Copy-Item -LiteralPath data/my-track.txt -Destination backups/my-track-before-change.txt
Get-FileHash -Algorithm SHA256 data/my-track.txt, backups/my-track-before-change.txt
Copy-Item -LiteralPath backups/my-track-before-change.txt -Destination data/my-track-restored.txt
Get-FileHash -Algorithm SHA256 backups/my-track-before-change.txt, data/my-track-restored.txt
```

確認雜湊相同，再於程式載入 `my-track-restored.txt`，確認控制點和畫面。
還原使用新檔名，驗證後才決定是否替換工作檔。重要備份另存其他磁碟；repository 的 ignore 不能代替備份。
若未來增加格式版本，以追加且相容舊資料的方式處理，先加入舊格式讀取與新格式往返測試。

## 回復版本與排錯

保留前一版可用的發布目錄；新版本異常時關閉程式，使用前一版 executable 與已驗證的軌道備份。
建置失敗先確認使用 x64 MSVC／clang-cl 與 `tools.json` 指定的原生工具版本。Clang 模式先執行 `scripts/setup-tools.ps1 -WithClang`。不要混用 MinGW 或其他架構的 libraries。
CI 失敗保留 test-results artifact；日誌與 issue 附件避免包含使用者私密路徑與軌道內容。
