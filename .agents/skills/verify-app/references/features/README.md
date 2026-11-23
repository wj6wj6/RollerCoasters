# 功能地圖

依使用者看到的功能拆檔。改動前先查下表，只讀相關的檔案。

| 改到的程式 | 功能檔 |
| --- | --- |
| `main.cpp`、`TrainWindow.*`、`GameMenu.*`、`KeyBinds.*`（`Controls` 畫面）、`SceneSettings.h`、`CallBacks.cpp`（Run、Reset、Grand Tour）、`Platform.H`／`PlatformLoop.cpp`、`GlfwInput.*` | [startup-and-panel.md](startup-and-panel.md) |
| `ArcBallCam.*`、`FlyCam.*`、`HeadLook.*`、`ChaseRig.*`、`TouchpadGestures.*`、`TwoFingerGesture.*`、`TrainView.cpp` 的輸入處理 | [cameras-and-input.md](cameras-and-input.md) |
| `TransformGizmo.*`、`EditKeys.*`、`TrackInsert.*`、`TrackHistory.*`、`ControlPoint.*`、`TrainView.cpp` 的編輯 | [editing.md](editing.md) |
| `Track.*`、`Folders.*`、`GameMenu.cpp` 的 Load Track／Save Track、`CallBacks.cpp` 的 `loadTrack`／`saveTrack` | [track-files.md](track-files.md) |
| `TrainModel.*`、`TrackRenderer.*` | [train-and-rails.md](train-and-rails.md) |
| `AdvancedLighting.*`、`PostEffects.*`、`TrainViewEffects.cpp`、`UnderwaterOptics.h`、`MatrixMath.h` | [lighting-and-weather.md](lighting-and-weather.md) |
| `LandWorld.*`、`LandTextures.*`、`PixelArt.*`、`WaterFlow.*`、`LandWaterMotion.h`、`Mobs.*`、`WorldShape.*`、`Frustum.*`、`CellQueue.h` | [island.md](island.md) |
| `WorldSave.h`、`worldReady()`／載入畫面、`Ui/*`（自繪介面元件：載入畫面、Esc 選單、快捷欄） | [startup-and-panel.md](startup-and-panel.md) |
| `OceanWorld.*` | [underwater.md](underwater.md) |
| `TexturePack.*`、`ZipArchive.*` | [texture-packs.md](texture-packs.md) |
| `AppControl.*`、`TrainViewControl.cpp`、`scripts/control-app.ps1` | 本頁「驗證工具本身」 |

## 全站回歸順序

`pwsh -File scripts/control-app.ps1 smoke` 依下列順序執行全部情境（`list` 列出參數與預期結束碼）：

1. `doctor`：環境、OpenGL 與 shader 路徑。
2. `tour-train`：啟動預設的 Grand Tour 展示。
3. `cameras-*`：五種相機。
4. `light-*`：固定管線、夜晚、下雨。
5. `world-*`：平坦編輯島、海底。
6. `track-*`：載入範例軌道；壞檔與缺檔必須失敗（結束碼 1）。
7. `pack-missing`：缺少的材質包必須失敗。
8. `usage-*`：錯誤參數必須回 2。

自動情境之後，再做改動相關功能檔裡的手動檢查。發布前則把所有功能檔的手動檢查都做一遍，最後記錄 Windows 版本、GPU／驅動與輸入裝置，並確認關閉視窗會結束程序。

## 驗證工具本身

- 有什麼：`--doctor`、`--screenshot` 與場景參數（`RollerCoasters.exe --help`）；`scripts/control-app.ps1`。
- 怎麼驅動：`AppControlCheck`（Linux 與 Windows 的 CTest）測試解析、軌道檔檢查、PNG 與畫面判讀；改到 `TrainViewControl.cpp` 時在 Windows 上跑 `smoke`。
- 假象：
  - 程式在畫出第一幀前就失敗時，不能留下舊的截圖；`control-app.ps1` 每次先刪除同名 PNG，預期失敗的情境也會檢查沒有留下圖。
  - `frame: PASS` 只代表畫面不是一片底色，景物擺錯照樣會過，所以截圖仍要親自看。
  - `--track` 先經 `checkTrackFile()`；改了 `CTrack::readPoints()` 的格式時要同步更新它，否則兩者對同一份檔案的判斷會不一致（`AppControlCheck` 會抓到）。
