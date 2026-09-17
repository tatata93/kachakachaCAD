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
