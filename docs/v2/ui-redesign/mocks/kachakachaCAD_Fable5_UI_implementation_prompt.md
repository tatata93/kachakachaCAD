<role>
あなたは kachakachaCAD V2 の UI 再構築を担当するリード実装エージェントです。
C++20 / Qt6 / OCCT の既存コードベースを調査し、既存の Geometry / Document / Transaction / Save-Load / Undo-Redo の能力を失わず、人間が普通に操作できる CAD フロントエンドへ接続し直してください。

この作業は「UI案を考える」仕事ではありません。
UIの大枠と操作思想は既に決定済みです。
あなたは既存repoを深く調査し、仕様を漏れなく実装し、PC上で検証する責任を持ちます。
</role>

<context>
kachakachaCAD は鉄道模型・板材工作を主目的とするCADです。

最終的な主要ワークフローは、

作図モード:
Wire / Curve / WorkPlane / 3D Surface を定義する

部品モード:
Profile / Face / Surface から Solid を作り、Solid / Face を編集する

製作モード:
Surface / Solid Face を板材として製作可能な ApproxPart へ近似し、
分割・結合・Relief Cut・曲げ・展開を行う

という役割分担です。

特に流線形の鉄道車両前面を、
断面Wire → 3D Surface → 近似部材 → 展開
という流れで製作できることが重要です。

現在の問題は「Backend機能は相当量存在するのに、人間向けのUI入力経路が弱く、self-testでは通るが実際には操作しづらい」ことです。

今回の目的はBackendを書き直すことではありません。
既存Backendを調査し、UIから正しく・一貫して到達できるようにしてください。
</context>

<reference_files>
必ず最初に、添付された以下の3ファイルを読んでください。

<document>
<source>kachakachaCAD_drawing_mode_UI_detailed.html</source>
<purpose>作図モードの最新Visual / Interaction仕様</purpose>
</document>

<document>
<source>kachakachaCAD_part_mode_UI_inventor_like_v2.html</source>
<purpose>部品モードの最新Visual / Interaction仕様。特にInventor型Extrudeを正本とする</purpose>
</document>

<document>
<source>kachakachaCAD_fabrication_mode_UI_detailed.html</source>
<purpose>製作モードの最新Visual / Interaction仕様</purpose>
</document>

これらは単なる雰囲気参考ではありません。
画面構造、Toolカテゴリ、右ペインの情報階層、Input Slot、Method選択、Model Explorerの構造、操作順は仕様です。

色、数pxの余白、アイコンの絵柄、フォント微差はQtとして自然に調整して構いません。

過去の古いUI mock、旧Shelf layout、旧GuideTable中心UIより、この3ファイルと本指示を優先してください。
</reference_files>

<important_behavior>
Fable 5として、この作業を長時間のend-to-end実装として扱ってください。

十分な情報が揃ったら実装を開始してください。
既に決まった仕様を再検討したり、採用しない案を延々比較したりしないでください。

ユーザー確認のために停止するのは、
・破壊的または不可逆な操作
・本当のscope変更
・ユーザーだけが提供可能な情報
が必要な場合だけです。

可逆的で仕様から自然に導かれる実装は、許可を取り直さず進めてください。

「次に～します」「実装できます」「続けますか？」でターンを終えないでください。
実際に作業を進め、完了または本当にblockedになった時だけ停止してください。

進捗を報告する時は、このセッションのtool出力・build・test・screenshot等で確認できた事実だけを報告してください。
未検証なら未検証と明記してください。

不要な周辺refactor、新機能、将来のためだけの抽象化を追加しないでください。
今回必要なUI再構築と、それに必要な最小限のadapter / backend interface変更に集中してください。
</important_behavior>

<working_memory>
長期作業で仕様を失わないため、repo内に以下を作成して正本として使用してください。

1. docs/v2/ui-redesign/UI_FEATURE_MATRIX.md
2. docs/v2/ui-redesign/TASK_LEDGER.md
3. docs/v2/ui-redesign/UI_LESSONS.md

UI_FEATURE_MATRIX.md:
現在repoに存在する関連機能を棚卸しし、新UIのどこから到達するかを記録する。

最低限:
ID | MODE | CATEGORY | TOOL | METHOD_VARIANT | EXISTING_BACKEND | NEW_UI_ENTRY | PREVIEW | HUMAN_TEST | STATUS | NOTES

STATUS:
NOT_STARTED
IMPLEMENTING
CODE_COMPLETE
TESTED
PC_VERIFIED
BLOCKED_BACKEND
BLOCKED_HUMAN

TASK_LEDGER.md:
実装単位と検証結果を追跡する。

UI_LESSONS.md:
ユーザーから確定した修正、実装中に判明した重要なUI原則、再発防止事項だけを短く記録する。
repoや既存文書に既に書かれている情報を重複保存しない。

この3ファイルを実装漏れ防止に使用してください。
「覚えているつもり」で進めないでください。
</working_memory>

<inventory_first>
実装前にrepoを調査してください。

目的は「長い監査文書を書くこと」ではなく、
既存機能を新UIへの移行中に消さないことです。

特に以下はTool名だけでなく、既存の作成方式・variant・optionまで確認してください。

Line
Circle
Arc
Bezier
Spline
Trim / Extend / Split / Join / Offset
Transform
WorkPlane
Surface creation
Extrude
Revolve
Loft Solid
Sweep
Thicken
Fillet
Chamfer
Shell
Face edit
Boolean
Approximation
ApproxPart
Unfold
Bend state
Geometry output

HTMLに書かれていない既存の有効なmethodが見つかった場合、
勝手に削除せず UI_FEATURE_MATRIX.md に追加し、
同じToolの「作り方」または適切なadvanced optionとして新UIへ収容してください。

例:
HTMLに円の方式が3個しかなくても、repoに正当に利用可能な4個目の方式が存在するなら失わない。

既存Backendに存在しない機能を、見た目だけ実装済みにしないでください。
未実装ならdisabled + 理由表示、またはBLOCKED_BACKENDとして管理してください。
</inventory_first>

<common_ui_contract>
全モードの操作文法を統一します。

基本:
Toolを選ぶ
→ 右ペインがそのTool専用になる
→ 必要なInput Slotがアクティブになる
→ 3D Viewで対象をクリック
→ 必要なら次Slotへ移る
→ 設定を調整
→ Preview
→ Enterまたは確定

EscはCancel。

Selection Firstは補助として残して構いませんが、基本操作に必須ではありません。

右ペインに複数Toolのコマンド一覧を同時表示しないでください。
右ペインは「現在のTool」または「現在選択中ObjectのProperty」だけを表示します。

Tool中は一般selection priorityをそのまま使わず、現在のInput Slotに適したgeometryを優先してください。

例:
Extrude Profile -> Closed Region / planar Face
Revolve Axis -> Line / Axis
Loft Section -> Profile / Wire
Fillet -> Edge
Approximation -> Surface / Face
Boolean Target -> Solid

Tool Input StateをPreview/Commitの正本にしてください。
Preview後にCurrent Selectionを再解釈して別の結果をCommitする構造は禁止です。

PreviewとCommitは同一のinput snapshotから生成してください。

Active Tool / Right Panel / Cursor / Selection Filter / Preview / temporary Snap / Status text を同期させてください。
Toolを切り替えたのに前Toolのpanelが残る状態を再発させないでください。
</common_ui_contract>

<model_explorer>
左ペインはVS Code Explorer型の階層Treeにします。

基本構造:
Project
├ Origin
├ WorkPlanes
├ Groups
├ Wires
├ Surfaces
├ Solids
├ Approximation
└ Generated / Output

Originは常に上部。
最低限:
Origin Point
X Axis
Y Axis
Z Axis
XY Plane
YZ Plane
XZ Plane

「まとまり」というUI名称は「グループ」に変更してください。

各Wire / Surface / Solid / WorkPlane / ApproxPart等を個別に表示・非表示にできます。
親Groupのvisibilityも持ちます。

Explorer selectionと3D View selectionは双方向同期です。

Explorer:
Wireを選択
→ 3Dでも選択highlight

Viewport:
Surface等を選択
→ Explorerで該当項目を選択し必要ならscroll

Explorerからrename / visibility / face-to-selection / group move / duplicate / delete / propertiesへアクセス可能にしてください。

Wire、Surface、押し出し結果、Solid、ApproxPart等をExplorerから削除可能にしてください。
依存関係がある場合は、何が参照しているため削除できないかを人間向けに表示してください。
</model_explorer>

<measurement>
測定は全モードで必ず表示・使用可能です。

最低限:
距離
角度
半径 / 直径
座標
面積

実行中Toolを破棄せず一時測定できる設計を優先してください。

例:
Loft Preview
→ Distance Measure
→ measurement終了
→ Loftへ復帰
</measurement>

<drawing_mode>
作図モードの責務:
Point / Wire / Curve / WorkPlane / 3D Surfaceの定義。

Solid生成は部品モード。
近似・展開は製作モード。

カテゴリ:
基本作図
曲線
編集
変形
作業面
面作成
注記
測定

上段Category + 下段Toolの2段Ribbon。

Circle:
最低限「中心+半径」「3点」「直径指定」。
既存Backendに他方式があれば回収。

Arc:
最低限「中心・始点・終点」「3点」「始点・終点・半径」。
既存方式があれば回収。

Bezier:
制御点方式。
少なくとも、
・次数
・制御点
・制御点をPoint Objectとして残す
・制御polygonを補助線として残す
・始点接線
・終点接線
を扱う。

Spline:
最低限、
・制御点
・通過点
・Fit / Approximation
の作り方。

少なくとも、
・degree
・continuity
・fit tolerance
・weight
・start/end tangent
・必要なsymmetry条件
・制御点をPoint Objectとして残す
・制御polygonを補助線として残す
を既存Backend能力に応じて表示。

編集:
Trim / Extend / Split / Join / Offset と既存Curve編集機能。

変形:
Move / Rotate / Mirror / Scale / Copy と既存機能。

WorkPlane:
New / Select / Set Current / 選択に正対 / Visibility。
Plane表示は薄いfill、破線border、name、origin、U/V方向。
Current / Selected / Otherを視覚的に区別。

3D Surfaceも作図モードで作成します。
これが製作モードの近似元になります。

主要Surface:
Planar
Ruled
Loft
Guided Loft
Boundary Fill
Curve Network / Gordon

さらにrepoに存在するOffset Surface、Revolved Surface等の有効な方式を棚卸しして失わないでください。

Surface Toolにも「作り方」を持たせます。
例:
Planar -> closed profile / outer+hole
Loft -> AUTO order / MANUAL LOCK
Guided Loft -> Sections + Guides
Boundary -> boundary / continuity options
Gordon -> U/V network

Planar Surface標準操作:
平面
→ 閉じた輪郭内部をクリック
→ Preview
→ Confirm

複数Wireで一つの閉領域を構成していても、Wireを一個ずつCtrl選択させないでください。

Surface生成は可能な方式についてnon-destructive Previewを持たせてください。
旧GuideSurfaceTableは内部構造として再利用して構いませんが、
通常ユーザーに「表へ追加→役割→表から作る」を強制しないでください。
</drawing_mode>

<part_mode>
部品モードの責務:
Solid作成 / Solid編集 / Face編集 / Boolean / 配置。

カテゴリ:
作成
形状編集
面編集
ブール演算
配置

作成:
Extrude / Revolve / Loft Solid / Sweep / Thicken

ExtrudeはInventor型操作を基準にします。

標準:
押し出し
→ Closed Profile Region または planar Face をクリック
→ extent / distance等を指定
→ Preview
→ Confirm

平面Faceを直接選択できなければ不合格です。

既存Solidのplanar Faceをクリック:
Face全体をprofileとして使用
→ Face normal方向のPreview arrow
→ distance等
→ Preview

Face上にWireが重なっていても、ExtrudeのProfile入力待ちではClosed Region / Faceを優先してください。

5本の線で一つの閉領域を構成している場合、
5本Ctrl選択ではなく輪郭内部クリックでProfileRegionを取得してください。
Outer/Holeもclosed regionとして認識してください。

Extrude extent/methodは最低限:
距離
面まで
2面間
すべて貫通
対称
非対称

Input:
Profile / Face
From
To
Target Solid

Operation:
New Solid
Add
Cut

Add/Cut時のみTarget Solidを要求。

Extrude output:
Solid only
Wire only
Wire + Solid
End Wire only
Custom

Custom:
body
start profile wire
end profile wire
side wires

全出力OFFは禁止。
body OFFならBoolean操作は無効。

Revolve:
Profile → Axis → angle → Preview。
全回転 / 角度指定 / 対称を最低限持つ。

Loft Solid:
Sections only / Guided / Centerline。
Section orderはAUTO / MANUAL LOCK。

Sweep:
Profile / Path / Guide。
既存方式を回収。

Thicken:
Surface / Faceを選択。
outside / inside / mid-plane / per-face等、既存能力を露出。

Shape Edit:
Fillet / Chamfer / Shell / Split / Join。
各Toolはrepoに存在する方式を「作り方」として表示。

Face Edit:
Push/Pull / Face Offset / Delete Face / Replace Face。
Faceを3D Viewから直接選択。

Boolean:
Add: Target → Tool
Cut: Target → Tool
Intersect: 2+ Solids

Target/Toolを選択順から暗黙推測させず、Input Slotとして見せる。

Placement:
Move / Rotate / Mirror / Copy / Pattern。
Patternは少なくともlinear / circular / pathを、Backend対応に応じて扱う。
</part_mode>

<fabrication_mode>
製作モードの責務:
3D Surface / Solid Faceを実際に板材で製作可能なApproxPartへ変換する。

基本:
Surface / Face
→ Approximation
→ Candidate
→ ApproxParts
→ Split / Merge / Relief / Radius
→ Bend State
→ Unfold
→ Generate Geometry

カテゴリ:
近似
部材編集
曲げ・展開
生成

Approximation:
近似Tool
→ SurfaceまたはSolid Faceをクリック
→ Candidate A/B/C
→ Preview
→ Confirm

複数FaceはTool中の追加クリックで入力可能。
Ctrl必須にしない。

最低限の作り方:
標準
少部品優先
精度優先
手動条件

候補には、
・part count
・max error
・average error
・geometry method
を表示。

候補思想:
A 少部品 / 高誤差
B 中間
C 多部品 / 低誤差

ただし候補形状・境界を固定hardcodeしない。
実geometryから生成する。

物理部材の優先モデル:
Plane
Cylinder
Cone
Developable / Ruled

内部Triangle meshは計算用途には使用可。
ユーザーの物理部品を大量Triangleへ分割しない。

ApproxPartは、
method
radius
error
boundary
AUTO / LOCK
を持つ。

AUTO / LOCK:
例 R 42.63 AUTO。
ユーザーが45へ変更したら R 45.00 LOCK。
LOCK値は再近似で勝手に上書きしない。
AUTOへ戻す操作を用意する。

Split:
line / position / candidate boundary 等。
Previewで部品数と誤差before/afterを表示。

Merge:
auto refit / preserve method / manual method 等。
結合後のmethodとerrorを表示。
誤差が増えてもユーザーが許容して結合できる。

Relief CutとSplitは別概念。
Relief CutではPhysical Partを1部品のまま維持。
SplitはPhysical Partを分割。

Bend State:
0〜100% slider。
0%=実際のflat/unfolded state。
100%=target shape。
0%を単純projectionにしない。
0/25/50/75/100 presetを持つ。

任意のBend Stateから通常Document Geometryとして、
Surface / Face / Wire等を生成可能。
ApproxPart自体は破壊しない。

Unfold:
AUTO / reference edge / multi-part placement 等。
ApproxPart + Reference Edgeを入力。
展開先はXY / WorkPlane / new WorkPlane / near source等をBackend能力に応じて扱う。

展開不能なら、日本語で理由を示す。
Diagnostic codeだけで済ませない。

Explorerでは近似結果を、
Approximation
└ NoseFrontApprox
   ├ Candidate
   ├ Parts
   ├ Relief Cuts
   └ Generated
のような一回の近似結果単位で管理する。

自動治具生成機能は追加しない。
</fabrication_mode>

<diagnostics_and_feedback>
通常ユーザー向け表示は、人間が理解できる文章を主にしてください。

例:
「輪郭が閉じていません」
「断面があと1本必要です」
「この部材は完全展開できません」

EXT-xxx / GEO-xxx / UI-xxx等のdiagnostic codeは詳細情報として残して構いません。

クリック可能なToolは機能すること。
Backend未実装ならdisabled + 理由、またはBLOCKED_BACKEND。
「押せるが何も起こらない」は禁止です。
</diagnostics_and_feedback>

<verification_strategy>
この規模では、自己評価だけで完了判定しないでください。

独立に検証できるsubtaskはsubagentへ委譲してください。
特に、
・既存機能inventory
・仕様とUI_FEATURE_MATRIXの照合
・Human-path test gap確認
・PC screenshotとHTML仕様の比較
は独立subagentに向いています。

実装本体を進めながらsubagentを並行利用し、最も遅いsubagentを待つだけの進行にしないでください。

各主要Stage終了時に、fresh-context verifierへ仕様と実装の照合をさせてください。
verifierは実装担当の自己説明を信用せず、コード・test・screenshotを確認してください。
</verification_strategy>

<execution_plan>
最初にrepoを調査して、適切なStageへ分けてください。
以下は最低限必要なまとまりです。細分化は実装都合に合わせて構いません。

Common:
Model Explorer / common Tool Controller / Current Tool Panel / Measurement

Drawing:
Basic + Curve
Edit + Transform + WorkPlane
Surface

Part:
Create
Shape + Face Edit
Boolean + Placement

Fabrication:
Approximation
ApproxPart editing
Bend + Unfold
Geometry generation

Integration:
Human-path regression
responsive/scaling
old duplicate UI cleanup

1 Stageを、
実装
→ build
→ tests
→ human-path check
→ PC screenshot
→ commit
→ ledger更新
まで閉じてから、そのStageを完了扱いしてください。

ただし独立Stageはsubagent等で並列調査して構いません。
</execution_plan>

<human_path_tests>
self-testがUUIDやhidden widgetを直接注入するだけではUI検証になりません。

必ず人間経路を守るテストを追加してください。

最低限、以下の利用シナリオをカバーします。

Drawing:
Circleの複数方式
Arcの複数方式
Bezier control points
Spline methods
Tool switch時のPanel同期
WorkPlane
Planar Surface
Loft
Guided Loft

Part:
5本Wireの閉領域内部click → Extrude
planar Face click → Extrude
Extrude To / Through / Symmetric
Extrude Add / Cut
Wire only / Wire+Solid / End Wire
Revolve
Loft Solid
Sweep
Thicken
Fillet / Chamfer
Push/Pull
Boolean
Move / Mirror / Pattern

Fabrication:
Surface → Approximation
Solid Face → Approximation
multi Face
Candidate switching
AUTO → LOCK
Split
Merge
Relief Cut
Bend 0 / 50 / 100
Unfold
Surface / Wire generation

Explorer:
individual visibility
Tree → View selection
View → Tree selection
delete
Group
Origin always top

Measurement:
各Modeでvisible/usable
実行中Toolへ復帰
</human_path_tests>

<responsive_requirements>
最低限以下をPC確認してください。

1280x720
1920x1080
2560x1440

Windows scaling:
100%
125%
150%

文字切れ、ボタン重なり、right panel欠落、Explorer崩壊、hidden Tool panelを許容しないでください。
</responsive_requirements>

<known_regressions>
少なくとも以下をRegressionとして固定してください。

Extrude panelがhiddenになる
Tool切替後も古いRight Panelが残る
Tool-firstでCtrlが暗黙必須になる
Faceを選びたいのにWireが常に勝つ
SurfaceにPreviewがない
GuideTableを知らないとSurfaceを作れない
Extrude distanceと板厚が同じparameter
20mm板厚上限がExtrudeへ漏れる
EnterがViewport focus依存
Tree / View selection divergence
個別visibility不足
「まとまり」表記
Originが最上段でない
狭い画面でUIが潰れる
hidden widgetを直接操作するだけのself-test
</known_regressions>

<scope_boundaries>
既存のGeometry / OCCT / Document / Transaction / Undo-Redo / Save-Loadを必要なく再実装しないでください。

UIに必要なBackend interfaceが不足する場合は、最小限のadapterまたはAPI追加を行うか、
本当に別担当が必要なら TASK_LEDGER に BLOCKED_BACKEND として、
必要な入力・出力・既存関連API・不足理由を具体的に記録してください。

テストを通すために、
test削除
skip
assertion弱化
expected値を実装へ合わせるだけの変更
をしないでください。

destructive git操作、他AIの履歴破壊、force pushをしないでください。
</scope_boundaries>

<completion_criteria>
「コードがある」「buildが通る」だけでは完成ではありません。

各機能は少なくとも、
UIから到達可能
現在のTool Panelが見える
Inputを人間経路で指定可能
Previewが正しい
Confirm可能
Cancel可能
必要なUndo/Redoを壊さない
Human-path testがある
PC上で画面確認済み
で完成です。

UI_FEATURE_MATRIXに
NOT_STARTED
IMPLEMENTING
CODE_COMPLETE
BLOCKED_BACKEND
が残っている状態で全面COMPLETEを宣言しないでください。

PCで未確認ならPC_VERIFIEDとは書かないでください。

進捗・完了の主張は、そのセッションのtool結果に紐づけてください。
</completion_criteria>

<communication>
作業中の説明は必要最小限で構いません。
実装を止めて長い設計解説を繰り返さないでください。

ただしStage完了時には、
何を実装したか
何を検証したか
何がまだ残っているか
実際のHEAD / test結果 / screenshot
を簡潔に報告してください。

最終報告は作業ログの続きではなく、ユーザーが初めて結果を見る前提で書いてください。
最初の一文で結果を述べ、その後に重要な検証結果と未完了事項だけを説明してください。

内部のchain-of-thoughtや思考過程をユーザーへ書き出す必要はありません。
</communication>

<task>
上記仕様と添付3HTML、現行repoを基準に、kachakachaCAD V2の作図モード・部品モード・製作モードUIをend-to-endで実装してください。

まず3HTMLと現行repoを読み、既存Tool / Method / Variantを棚卸しして UI_FEATURE_MATRIX.md と TASK_LEDGER.md を作成してください。

その後、計画だけで止まらず実装を開始してください。

既存Backend機能を失わず、HTMLに記載されていない既存の有効なMethodも新UIへ収容してください。

完成判定は見た目の変更ではなく、
「ユーザーが Tool → 対象選択 → 設定 → Preview → Confirm の一貫した操作で実際に作業できること」
です。

特に最重要シナリオとして、
・作図モードで3D Surfaceを作れる
・ExtrudeでClosed Region内部またはplanar Faceを直接選べる
・製作モードでSurface / FaceからApproximation → ApproxPart → Bend → Unfoldまで進める
ことをPC上で実証してください。
</task>
