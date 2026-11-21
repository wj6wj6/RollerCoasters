---
name: verify-app
description: 驗證 RollerCoasters 的改動在實際程式裡真的有效。改了顯示、輸入、相機、軌道、場景、光影或材質包之後使用；先讀功能地圖中相關的那一份，再用 scripts/control-app.ps1 取得截圖與 doctor 輸出作為證據。
---

# 驗證 RollerCoasters 的改動

「build 過了」「CTest 綠了」不算證據：CTest 不畫 GPU 畫面，shader 失敗時程式也會靜默退回固定管線，看起來照樣能跑。
改完之後依下列順序做，並把證據放進 PR。

## 1. 找出相關的功能檔

讀 [references/features/README.md](references/features/README.md) 的對照表，只讀這次改動碰到的功能檔，不必讀全部。
每份功能檔回答四件事：有什麼功能、使用者怎麼到達、怎麼用 CLI 驅動、哪些假象最容易騙過檢查。

## 2. 確認環境能取得證據

程式只能在 Windows 上執行（Win32 視窗程序與輸入、觸控板的 Direct Manipulation）。

- 有 Windows 且有顯示卡：先建置，再跑 `pwsh -File scripts/control-app.ps1 doctor`。任何 `FAIL` 都要先處理，尤其是 `shader lighting` 與 `post effects`。
- 只有 Linux（例如雲端環境）：跑 `docs/development.md`「Linux 上的檢查」，並在 PR **明確寫出畫面證據尚未取得**、列出需要在 Windows 上跑的情境。不可以用 Linux 檢查代替截圖。

## 3. 依序走完功能檔裡的路徑

每份功能檔列出適用的路徑，依下列順序執行；不適用的路徑寫明原因：

1. **成功**：正常操作的結果，用 `screenshot` 或 `smoke -Only <前綴>` 取得截圖。
2. **取消**：遊戲內畫面按 Cancel、No 或 Esc、Esc 取消編輯，狀態要維持原樣。
3. **錯誤**：壞檔、缺檔、錯誤參數，必須是預期的結束碼（1 或 2），而且沒有留下會被誤當成證據的截圖。
4. **空狀態**：沒有材質包、`Scenery` 關閉、控制點最少（4 點）等邊界。
5. **持久化**：Save 後重新 Load 的往返結果；換材質包、切相機後再切回來，狀態都要正確。

改動之後一律再跑一次全站回歸：`pwsh -File scripts/control-app.ps1 smoke`。

## 4. 交出證據

PR 的「驗證」段落附上：

- `out/verify/smoke.txt` 的結果表，以及 `doctor` 的輸出（含 GPU 與驅動版本）。
- 改動相關情境的截圖（`out/verify/*.png`）。截圖要真的看過；`frame` 檢查只能證明畫面不是一片底色，不能證明景物擺對。
- 只能手動檢查的項目（觸控板手感、輸入法、Load Track／Save Track 畫面）：寫出裝置、步驟與結果。
- 沒做的檢查如實列出原因。

## 5. 維護功能地圖

改到使用者看得到的行為時，同一個 PR 要一起更新：

- 對應的功能檔（行為、到達方式、假象）。
- 新的可自動化情境加進 `scripts/control-app.ps1` 的 `$Scenarios`，並在功能檔寫出情境名稱。
- 新增功能檔時加進 `references/features/README.md` 的對照表與回歸順序。
- 新增命令列參數時同步更新 `src/AppControl.*`、`tests/AppControlCheck.cpp` 與 `docs/operations.md`。
