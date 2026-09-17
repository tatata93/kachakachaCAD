# V2 立体・面・近似 UI 全面改訂 引継ぎ

更新日: 2026-09-17  
対象: `main` に統合する `codex/ui-frontend-redesign` の `796148e..HEAD`

## 正本

- オーナー指示: `kachakachaCAD V2の立体・面・近似系UIを全面的に再設計してください。`
- 操作文法: **道具を選ぶ → 3D画面で対象を選ぶ → 条件 → 下見 → 確定**
- 選択済みで道具を押す経路は補助。選択していなくても全操作を開始できること。
- ユーザーへ Wire ID や内部の役割表を基本操作として要求しない。

## この版で完了した範囲

### 閉じた輪郭領域

- `ProfileRegion` は文書へ保存しない一時的な入力表現。
- 直線、円弧、Bezier、Splineを混在可能。
- 5本以上の別Wire、1 Entity内の複数segment、別Entityの組合せを扱う。
- 穴、独立した複数領域、別平面を区別する。
- 開いた線と分岐線は領域にしない。
- 同じ端点を持つ独立した閉Wire Entityを分岐と誤認しない。

### 3D画面での領域選択

- 押し出し/平面Surface中は輪郭内をhoverすると半透明緑、選択すると橙で表示。
- 輪郭線付近も同じ領域として拾える。
- 素のクリックで追加、選択済み領域の再クリックで解除。Ctrl不要。
- 穴の内部は外周領域として拾わない。
- 複数構成線は3D上でも `PROFILE 1` という一つの論理入力として表示。

### 押し出し

- 道具を先に押した直後から押し出し専用棚を前面表示。
- 空入力では輪郭欄が選択待ちになり、3D内クリック後ただちに下見を生成。
- 下見中も追加の領域/対象をクリック可能。
- 複数領域と穴を既存Extrude backendへordered segmentとして渡す。
- Face押し引きではSolid/Faceのpickを領域より優先する。
- Enter/Escは右棚の入力欄にfocusがあっても動作する。

### 面を作る

- 選択なしで開始してもSurface専用棚を即表示する。
- 空入力の既定は平面境界。閉じた領域を内側クリックすると即下見。
- 5本以上の構成線、穴、複数領域をGuideTableの外形/穴へ変換する。
- 元Wire IDは重複除去して保存・再評価へ渡す。
- 確定までDocumentを変更しない。

## 検証

- Windows Release build: 成功
- `ctest`: この変更前の全150件中149件成功、残りのarchitecture header規則を修正後に単独成功
- `kachakacha_cad_next.exe --self-test`: 250 passed / 0 failed
- Human path:
  - 5本の別Wireを面と押し出しへ内側1クリック
  - 穴付き輪郭
  - 独立2領域をCtrlなしで追加
  - 再クリック解除
  - 開いた線を拒否
  - SolidのFace押し引き
- Windows実描画画像:
  - `build-codex-region-917/PROFILE-02-hover-final.png`
  - `build-codex-region-917/PROFILE-02-preview-final.png`

## 次に実装する順序

1. **Surface入力slotの明示状態**
   - `surfacePickRole_`相当をMainWindow/UI stateへ追加。
   - 断面/ガイド/境界の欄を押すと、その後のviewport clickがその欄だけへ入る。
   - 各入力に解除と選び直しを設ける。
   - 選択解除をSurfaceInputへ同期し、古い入力を残さない。
2. **SURF-02 / SURF-03を完全なtool-firstへ**
   - Loft: Section欄へ順番にクリックして即Preview。
   - Guided Loft: SectionからGuideへ明示遷移。
   - AUTO採用順を常時表示し、MANUAL LOCK順をkernel requestへそのまま渡す。
3. **近似専用のtool-first入力層**
   - `近似`押下直後に専用棚を表示し、Face/Surfaceだけpick可能にする。
   - 単Face/Surface/複数Faceを素のクリックで追加。
   - A/B/C候補を部品数・最大誤差付きで比較し、選択候補だけPreview。
   - 既存`FabricationDock`の工程タブとbackendは再利用し、作り直さない。
4. **Boolean専用slot**
   - 足す/引く開始でTarget待ち、入力後Tool待ちへ自動遷移。
   - Target/Toolを3D札と右棚に別表示し、解除/再選択を付ける。
   - 非破壊Preview後にEnterで1 Undo単位として確定。
5. **ApproxPart編集/展開/曲げ状態**
   - AUTO/LOCK、分割/統合/Relief Cut、展開基準辺を一つの編集棚へ整理。
   - 0〜100%と実寸曲げ値を同期。
   - 任意曲げ状態からWire/Surfaceを生成しても元ApproxPartを残す。
6. **残る立体道具**
   - Fillet/Chamfer/Revolve/Unfoldを同じTool→slot→Preview→Confirm文法へ揃える。
7. **画面証拠**
   - Planar、Loft、Guided Loft、Approximation、BooleanのWindows実描画を追加。

## 触る場所

- 領域検出: `src/next/kachakacha/app/ProfileRegion.*`
- 領域pick/描画: `src/apps/cad_next/V2ProfileRegionPicking.cpp`
- 押し出し状態: `V2PendingCommand.cpp`, `V2ExtrudeInteractive.cpp`, `V2ExtrudePlanCommand.cpp`
- Surface状態: `V2SurfaceCommands.cpp`, `V2SurfaceDock.*`
- 選択通知: `V2MainWindow.cpp`の`SetSelectionChangedCallback`
- 役割札: `V2ToolRoleLabels.cpp`
- 人間操作試験: `V2SelfTestHumanPath.cpp`

## 守ること

- backendをUI都合で複製しない。
- Previewと確定は同じsnapshot/requestを使う。
- hidden widget直接操作やUUID注入だけをHuman-path合格にしない。
- 既存V1を削除しない。
- `MainWindow.cpp`を増やさず独立ファイルへ置く。
- Windowsでbuild、ctest、self-testを通し、画像を目視してから完了とする。

## 進捗(Claude、2026-09-17、main d3893fa 起点、雲で作成・PC 未検証)

「次に実装する順序」の 1〜7 を、雲で core build / ctest 145 / Qt 当て木 88 files まで
通した状態で入れた。**Windows の build・ctest・自己試験・画像の目視はまだ**(PC の
監視が止まっており、PC の HEAD が `codex/v2-ui-ux-overhaul` にあるため)。

| 順 | 中身 | commit | 主なファイル | 自己試験 |
| --- | --- | --- | --- | --- |
| 1 | Surface 入力 slot の明示状態(ここへ選ぶ / 解除 / 押し直しで外す) | 9733ba0 | app/SurfaceInputState, V2SurfaceSlots.cpp, V2SurfaceDock | HP-SF-06 |
| 2 | Loft / Guided Loft: 採用順を常時表示、MANUAL LOCK を kernel まで | b13ddbc | modeling/GuideSurfaceInput(keepSectionOrder), GuideSurfaceTable(lockSectionOrder) | HP-SF-07 |
| 3 | 近似の tool-first(3候補を実際に作って比べる、下見 → Enter) | 7c223b0 | app/ApproxInput, V2ApproxCommands.cpp, V2FabricationDock | HP-AP-01/02, HP-FAB-01 |
| 4 | Boolean 専用 slot(土台 → 相手 自動遷移、札、下見、Transaction で 1 undo) | 7a5f7a0 | app/BooleanInputState, V2BooleanDock, V2BooleanCommands.cpp, Shelf::Boolean | HP-BO-01/02 |
| 5 | 部材の編集を曲げの段へ、組立率 ⇄ 半径の言い換え(PercentForRadius) | 3b75066 | fabrication/BendRadius, V2FabricationDock | 組立率と半径… |
| 6 | 面取り/丸め(道具 → A → B → 下見 → Enter)、回転体(断面 → 軸 自動遷移) | 1c50e3c | V2CornerPreview.cpp, app/ToolTargeting(ClickPicksPairMember), SurfaceInputState(軸) | HP-CN-01/02, HP-SF-08 |
| 7 | 画面証拠の場面 ui-guided-loft-preview / ui-approx-candidates / ui-boolean-preview | 7038713 | V2UiShotStatesMore.cpp, pc-fix-and-build.reference.cmd 08〜10 | (撮影は PC) |

道具の入口は 1 か所(`BeginToolFirstCommand`、V2BooleanCommands.cpp): 面を作る・近似・
足す引く・回転体・面取り/丸めは、構えて待つ道(ArmCommand)を通さない。
既存の自己試験で `fabrication.create` を 1 回押して即座に作っていたものは
「一度目は構えて下見、二度目で確定」に直した(押し出しと同じ)。

展開(型紙)は変えていない。既に 製作モデル → 型紙の下見(PatternDock) → 書き出し で
同じ文法になっている。

### PC で確かめること(順)

1. `git checkout codex/ui-frontend-redesign`(= d3893fa)にしてから `to-pc.bundle` を
   `_SAFE_MERGE.ps1` で ff 取り込み(`cloud-next`)。
2. `_FIX_AND_BUILD.cmd`(08〜10 の撮影を足した版を同梱)で build / ctest / 自己試験。
3. `_claudeout\ui\08_ui-guided-loft-preview.png` 〜 `10_ui-boolean-preview.png` を目視。
4. 赤があれば `_claudeout\run.txt` の Explain の行をそのまま雲へ。
