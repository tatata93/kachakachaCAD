# Wire-first V2 受入試験

## 1. 完了の原則

- UIが表示されるだけでは未完成。
- 三角形previewが出るだけではPartまたは型紙の完成ではない。
- 例外が出ないだけでは成功ではない。
- 自動試験、Windows実機試験、出力再検査、画面確認の全てを必要な範囲で通す。
- 試験で検証していない必須要件を「実装済み」と記録してはならない。

## 2. 試験レベル

| レベル | 内容 | 実行先 |
| --- | --- | --- |
| Unit | 曲線、鎖、式、許容差、DAG、コマンド | 全OS |
| Core integration | 文書、Feature、近似、型紙 | 全OS |
| Kernel integration | OCCT B-Rep、Boolean、STEP/STL | Windows/CI |
| UI integration | Qt command state、入力、selection | Windows/CI offscreen |
| Visual | 3D/型紙/テーマ/DPI画像 | Windows |
| Scenario | 初心者フロー、失敗回復、保存再読込 | Windows |
| Distribution | 配布zip、DLL、ライセンス、実起動 | Windows |

## 3. Fixture

V2は次のfixtureをコードまたは `.kcd2` で持つ。

```text
v2-empty
v2-wire-mixed-loop
v2-wire-branched
v2-planar-profile-with-hole
v2-extrude-two-islands
v2-wire-cage-box
v2-guided-loft-connected-ends
v2-gordon-network
v2-cylinder-panel
v2-cone-panel
v2-double-curvature-shoulder
v2-opening-crosses-panels
v2-er1-er2-round-cab-1-87
v2-corrupted-document
```

- Fixture IDは固定UUID。
- 期待値はmm/radでソースへ明記する。
- 実車不明寸法を実寸と表記しない。
- golden画像だけを期待値にしない。寸法、位相、曲線種類、エラーcodeも比較する。
- 浮動小数点の文字列完全一致ではなく文書化した許容差を使う。

## 4. データ・依存試験

### AT-ARC-001 依存境界

V2 coreの全public headerを単独compileし、Qt/OCCT headerをincludeしない。Qt targetからcoreへの依存は許すが、
逆方向のlink/includeをCMake検査で拒否する。domain headerからmodeling/fabrication headerへの依存も拒否する。
対応: `DOC-001`から`DOC-005`。

### AT-ARC-002 強い参照型

EntityId/FeatureId/GroupId/SegmentId/PanelId/FoldId/CutId/AssetIdの相互代入、raw stringからの暗黙変換が
compile失敗するnegative compile testを持つ。
Feature定義に `sourceName`、`targetName`、`objectName` という参照用fieldがないことを静的検査する。

### AT-ARC-003 単一の正本

Feature parameterを変更し、Entity metadata、UI model、OCCT cacheに古い幾何定義が残らないことを検査する。
serialize対象にpreview meshと通常のderived B-Repが含まれない。

### AT-ARC-004 Diagnostic registry

全Diagnostic codeが一意で、非空の日本語summary、severity、必要なrecovery actionを持つ。
UI層が日本語本文の文字列比較で分岐していないことを検索試験する。

### AT-ARC-005 V2コード衛生

V2新規ファイルの行数上限、MainWindow shell上限、禁止include、絶対path、必須経路のTODO/stub、
常に成功するplaceholderをCI scriptで検査する。例外許可はADR番号付きコメントを必要とする。

### AT-ARC-006 製作幾何境界

同じB-Repへ表示meshの粗さを3段階設定して製作近似し、panel topology、境界曲線種類、最大/RMS偏差が
規定許容差内で同じであることを検査する。pure fabrication testはfake `IFabricationGeometryProvider` でOCCTなしに
実行でき、OCCT integration testはproviderが世代一致するB-Rep cacheを参照することを検査する。

### AT-DOC-001 UUID参照

同じ表示名のWireを3本作り、1本だけをExtrude入力にする。保存再読込、rename、並べ替え後も同じEntityIdを
参照し、別Wireへ変わらない。対応: `DOC-010`以前、`PRD-001`から`PRD-005`。

### AT-DOC-002 DAG再計算

A wire -> GuideSurface -> FabricationModel -> Patternを作る。Aだけ変更し、下流3つだけrevisionが増え、
無関係Entityは再評価されない。同じ入力を2回評価した結果順が同じ。

### AT-DOC-003 循環拒否

下流Featureを上流入力へ設定しようとするとcommit=false、code=`DOC-CYCLE`、文書のserialize結果が
操作前と同じ。

### AT-DOC-004 トランザクション

有効Partに自己交差Wireを追加して再計算を失敗させる。直前の有効PartをStale表示で残し、半端なEntity、
Feature、ID、Undo項目を追加しない。

### AT-DOC-005 Undo/Redo

点、Wire、押し出しを作成し、連続dragを1回、Undo/Redoする。EntityId、FeatureId、表示順、参照が戻る。
100回往復して参照欠損しない。

### AT-EXP-001 保存再読込

全Entity kind、全Feature type、重複表示名、active group、grid、表示状態と表示設定、Part用途・材料・
基準板厚・縮尺、数式、型紙配置、個別Fold overrideを保存し、再読込後のsemantic documentが一致する。

### AT-EXP-002 壊れた保存

切断ZIP、欠損JSON、重複UUID、欠損BREP、未来schemaを各々拒否し、開いているDocumentを変更しない。
codeを固定する。

### AT-EXP-003 旧形式

旧テキスト `.kcd` は `KCDV2-F001` で拒否し、空Documentとして誤読しない。

### AT-EXP-004 原子的保存

既存ファイルを用意し、書込途中の失敗を注入する。既存ファイルが再読込可能なまま残る。成功時は新ファイルと
`.bak` が再読込できる。

## 5. ワイヤー試験

### AT-WIR-001 曲線値

Line、Arc、Circle、Bezier、B-splineの始終点、導関数、弧長、分割、最近点を解析解または高精度基準と比較する。

### AT-WIR-002 混成閉輪郭

2直線+2円弧+1Bezierを別Wireとして順不同・方向反転で渡す。1つのclosed WireChainへ決定論的に整列し、
元Entityを変更しない。

### AT-WIR-003 混成open断面

直線+円弧+B-splineを複数Wireからopen chainへ組み、端点、全弧長、方向が期待値と一致する。

### AT-WIR-004 分岐と隙間

T字分岐は `GEO-W002`、0.005mm隙間は既定model toleranceで `GEO-W003`。明示Join後だけ有効になる。

### AT-WIR-005 Segment内部接続

断面端が外形Arcのparameter 0.35へ接する。元Arcを分割せずSegmentRef rangeとして接続を保持する。

### AT-WIR-006 トリム/延長

全必須曲線種類で、trim前後の種類、交点、接線、弧長を検査する。Line/Arc/Bezier/B-splineの延長が
元曲線条件を維持する。

### AT-WIR-007 円弧作図

3点、両端+半径の両解、始点+接線、始点+法線、中心角、円弧長を作り、入力条件を数値検査する。
不可能半径は必要最小値付きで拒否する。

### AT-WIR-008 数式

`(180/2)*3`、単位変換、pi、deg/rad、全角正規化、0除算、型不一致、構文エラーを検査する。

### AT-WPL-001 作業平面の全方式

標準、offset、点を通り平行、2面の中間、円筒中心、辺まわり角度、3点、2辺、辺を通り接する、
点を通り接する、点で曲線に直角を既知形状から作る。原点、右手系基底、法線、距離、角度を数値検査する。

### AT-WPL-002 作業平面の不可能条件

一直線上の3点、平行でない中間2面、円筒でないFace、ゼロ長辺、曲率法線を定義できない直線上の条件を
固定Diagnosticで拒否し、既存Documentを変更しない。

### AT-WPL-003 平面とWireの関係

Free3D、ReferenceOnly、LockedToPlaneのWireを作り、作業平面を移動/回転する。Lockedだけが平面内UVを保って
追従し、他2つの3D座標は変わらない。2D/3D作図の出力が同じWire3d型である。

## 6. 形状ガイド試験

### AT-GEO-001 平面輪郭と穴

曲線を含む外周と円穴からPlanarBoundaryを作る。穴数1、面積、境界曲線種類を検査する。

### AT-GEO-002 2断面

複数Segmentのopen断面2群からRuledSectionsを作る。断面を厳密に通り、向き反転入力でもねじれない。

### AT-GEO-003 多断面

Segment数が異なる5断面からLoftSectionsを作る。各断面偏差、法線連続、自己交差なしを検査する。

### AT-GEO-004 両端接続ガイド

2本の複合外形ガイドが両端で接続し、3本の複合断面端が各ガイド内部へ接するfixtureをGuidedLoftする。
全ガイドと断面を許容差内で通る。ユーザー提示ケースの回帰試験。

### AT-GEO-005 Gordon網

3本のU鎖と4本のV鎖を順不同で渡し有効面を作る。交差1個欠損、2重交差、順序逆転をそれぞれ固定codeで拒否する。

### AT-GEO-006 Boundary fill

3辺と4辺の非平面境界をG0/G1で作る。5辺は自動三角分割せず拒否する。

### AT-GEO-007 壊れた参照

入力Segmentを削除し、GuideSurface参照が別の近いSegmentへ移らずBrokenReferenceになる。

### AT-GEO-008 役割表から面を作り、開き直しても戻る

役割表を人が組み立てて面を作る道。作り方を7通りから選び、使わない役割の行が残っていれば断る。
選んだ線を役割(外形U/外形V/断面/外形/穴/境界辺)付きで行にし、既存行の端へ線を足し(逆向きの線は
向きを直して足す)、行を上下に動かし、向きを反転し、行を消す。表から面を作ると、役割・向き・
作り方・離す距離が Feature に保存され、保存して開き直すと同じ表・同じ面が戻る。
元の線が消えていれば理由(GEO-R002)を出して断り、近い線へ移らない。
おまかせ(guide.create)は従来どおり選んだ順の断面で作り、同じ保存の形を使う。

## 7. Partと押し出し試験

### AT-GEO-010 Wire cage box

12辺を順不同で渡し6平面patchと1 closed solidを検出する。体積、bbox、Face provenanceを検査する。

### AT-GEO-011 不完全shell

1辺欠損、重複辺、T字辺、self-intersectionをそれぞれ拒否し、クラッシュも空Part登録も起こさない。

### AT-GEO-012 候補の限定

2つの離れたbox wire cageがDocumentにある。片方の選択だけから候補を作り、もう片方を自動採用しない。

### AT-GEO-013 非連結結果

2つの閉solid候補を同時確定すると、1つのmulti-solid Partではなく2 Partのプレビューとcommitになる。

### AT-EXT-001 基本押し出し

40x20mm矩形を30mm押し出す。体積24000mm3、bbox、6 faces、意味的SubshapeKeyを検査する。

### AT-EXT-002 開Wire

3Segment open chainで終端輪郭と側面境界Wireを作れる。PartチェックをONにすると `EXT-002` で拒否する。

### AT-EXT-003 WireとPart同時

同じ押し出しから終端Wire、側面Wire、Partを同時生成する。WireとPart境界の点が `modelLinearMm` 内で一致する。

### AT-EXT-004 穴付き輪郭

矩形外周+円内周を押し出し、貫通穴のある1 Partを作る。円が多角形Wireへ変わらない。

### AT-EXT-005 複数外周

非接触矩形2つを新規押し出しし、2 Partになる。事前表示の個数とcommit個数が一致する。

### AT-EXT-006 終端方式

Distance、SymmetricDistance、TwoDistances、平面ToTarget、曲面ToTarget、ThroughAll cutを数値検査する。
到達しないToTargetは部分solidを残さない。

### AT-EXT-007 足す/引く

重なる2 Partのadd、穴cut、Partを2つへ分離するcutを検査する。分離cutは事前確認なしに元Partを置換しない。

### AT-EXT-008 連続編集

profile寸法、押し出し距離、target位置を変更し、同じoutput EntityIdで形状だけが再計算される。

## 8. 測定試験

### AT-MEA-001 2点XYZ

点(1,2,3)と(4,6,15)でdistance=13、dX=3、dY=4、dZ=12、投影距離、軸角度を検査する。

### AT-MEA-002 3点角度

既知の30、90、150degと退化入力を検査する。

### AT-MEA-003 曲線法線

Circle、Arc、Bezier、B-splineの接線、曲率法線、半径/曲率半径を検査する。Lineは `MEA-001`。

### AT-MEA-004 直線と曲線

交点を持つLine/Arcの接線角度と法線角度を検査する。見かけ交差だけの場合は `MEA-003`。

### AT-MEA-005 測定から作図点

測定点、円中心、Bezier/B-spline制御点から作図点Featureを作る。位置とactive group所属を検査する。

## 9. 製作近似試験

### AT-FAB-001 円筒

円筒面帯を1つのCylindrical panelへし、型紙が解析的な長方形寸法と一致する。三角形部材を生成しない。

### AT-FAB-002 円錐

円錐台帯を1つのConical panelへし、型紙が扇環寸法と一致する。

### AT-FAB-003 二重曲率

二重曲率shoulderは切れ目OFFで目標偏差未達を報告する。切れ目ONでCurvedVNotchまたは追加分割を使い、
最大偏差を改善する。

### AT-FAB-004 切れ目方向

U、V、Both、Autoで候補が変わる。BothがU/Vの一方に固定されず、交差/ligament検査を通る。

### AT-FAB-005 1枚/少数/分割/混合

同じsourceで4 strategyを作り、OnePieceは1連結、SeparatePanelsは各panel別、FewPiecesは目標偏差内の
最小候補、Hybridは強曲率部だけ分離する。

### AT-FAB-006 手動役割

既存WireへBoundary/Fold/Relief/Opening/KeepTogether/NoCut/BendDirectionを割り当てる。
矛盾役割を拒否し、保存再読込で参照が維持される。

### AT-FAB-007 開口またぎ

円と角丸長方形の開口が3 panelをまたぐ。型紙各片を組み立てると閉開口へ戻り、曲線偏差が目標以内。
開口数、junction数、MatePairを検査する。

### AT-FAB-008 切れ目対応辺

Straight、V、CurvedVの左右辺parameter対応を検査する。100%でmate gapが目標以内。片辺長だけを縮めない。

### AT-FAB-009 組立寸法不変

0、10、30、50、90、100%で全boundary edge、代表測地距離、平面panel scaleを比較する。
規定相対誤差を超えず、triangle flipが0。30%の画像でしわがない。

### AT-FAB-010 閉ループ

接着MatePairを含む閉ループをsolveし、scale/shearなしでclosure gapを検査する。解けないfixtureは
最大残差Pairを返し、変形して成功扱いにしない。

### AT-FAB-011 任意状態Part

30%でPart化し、保存再読込後も30%。0/30/100%の対応辺長が一致し、形状bboxはそれぞれ異なる。

### AT-FAB-012 選択部材出力

10部材中2部材を選び、STL/STEPへ2 connected componentsだけを出す。非表示の別部材を混ぜない。

### AT-FAB-013 ER1/ER2 1/87

次を全て検査する。

- 幅基準は3520/87mm。その他の近似寸法はtest geometry表記。
- 腰部、窓帯、額が全面三角形でなく大きな連続panel。
- 強二重曲率の肩にだけreliefまたは追加分割。
- 6枚窓と中央前照灯の開口/接続を保持。
- fidelity 3/6/9で最大偏差が単調非増加。
- panelCountLimitを超えない。
- 30%にしわ、縮尺変化、開口消失がない。
- 100%のclosure gapが目標偏差内。
- 選択panelの1:1 PDF、30% STEP、100% STLが再読込検査を通る。

### AT-FAB-014 任意状態のワイヤーと部品

4部材のfixtureを30%へし、境界、折り、切れ目、開口を曲線種類付き3D Wireとして得る。
`ワイヤーのみ / 部品のみ / 両方` を順に固定し、Entity kindと個数を検査する。両方の場合は同一評価bundleから
生成され、全Part境界が対応Wireへ `modelLinearMm` 内で一致する。固定前はDerivedで直接編集不可、固定後は
元FabricationModelを変更しても独立したSource/Frozen出力として変化しない。

### AT-FAB-015 帯近似と接続スコープ(V1方式)

球のように二重に曲がった面を、面の分類(V2方式)は目標偏差を理由に断り、帯近似(V1方式)は
帯へ切って目標偏差に収める。同じ面・同じ目標偏差で、答えが違うことを検査する。
展開はレールと素線の長さを 3D と 2D で一致させ、曲げ具合 0 は平面、1 は近似形状に一致し、
途中も辺長を保つ。接続スコープの線は、近似メッシュに載っている点だけが近似形状へ寄り、
離れている点は元のまま残る。寄せた線は元の線とは別の「_接続」の線として作られ、
元の線は変わらない。近似モデルと曲げ状態は文書に保存され、開き直しても作り直せる。

## 10. UI試験

Qt Testまたは既存self-testの独立ケースとして実装し、1件失敗で残りを停止しない。

### AT-UIX-001 モード

4モード、共通操作、選択維持、右パネル同期を検査する。旧 `面/板材` モードがない。

### AT-UIX-002 操作ガイド

主要全コマンドでツール名、現在手順、次手順、確定、取消、選択数が存在する。
placeholder文や空欄を許さない。

### AT-UIX-003 カーソル入力

直線、円、円弧、押し出しで最初の主要欄へfocusし、式入力、Tab、Enter、Esc、画面端再配置を検査する。

### AT-UIX-004 スナップ

交点と主点が近いと交点を選ぶ。Shift中は自由点。右クリックで全候補。ringサイズとラベルを画像確認する。

### AT-UIX-005 グリッド

1/2、1/3、1/4の点数、主副サイズ、zoom省略、XYZ/UV数値とdragによる原点移動、Undo、保存再読込を検査する。
作業平面移動後も同じUVで追従し、壊れた平面参照を別平面へ付け替えない。

### AT-UIX-006 作業中グループ

active groupを切替後、Point/Wire/WorkPlane/GuideSurface/Partを作り全て所属する。
Derivedは派生groupへ入る。保存再読込でも維持。

### AT-UIX-007 形状ガイド表

複数行、行への複数Wire追加、順序変更、方向反転、両端接続表示、3D色同期を検査する。

### AT-UIX-008 View cube

drag量とcamera quaternion、cube quaternionが一致する。release後1秒で変化0。90deg snapなし。
世界/相対XYZ、0.05/0.25/1deg感度、15deg clickを検査する。

### AT-UIX-009 失敗回復

不完全shell、無効押し出し、近似失敗を各3回実行する。アプリが継続し、Document不変、入力値と選択を保持する。

### AT-UIX-010 Theme/DPI

Windows 95/標準theme、100/125/150/200% DPI、1366x768と1920x1080で主要画面を撮る。
文字切れ、重なり、画面外ボタン、0サイズviewport、操作不能scrollを許さない。線種、線幅、色、面色、
背景色を変更して保存再読込し、両themeで同じ値と配置を維持する。

### AT-UIX-011 コマンド台帳

`command-catalog.md` の必須IDと実装registryを双方向比較し、重複、欠損、台帳外IDを拒否する。
全Descriptorに日本語label、icon、selection predicate、操作ガイド、acceptance IDがあり、Document変更controllerが
DocumentCommandを返すことを検査する。menu/toolbar/shortcutが同じcontrollerへ到達し、service未接続の空controllerがない。

## 11. 出力試験

### AT-EXP-010 STEP/STL同一形状

同じPartをSTEP/STL出力し、STEP volume/bboxとSTL mesh volume/bboxが出力精度内で一致する。

### AT-EXP-011 無効形状

open shell、zero volume、self-intersection、厚み未指定FabricationModelを拒否し、0byteファイルを残さない。

### AT-EXP-012 SVG/DXF曲線

Line/Arc/Bezier/B-splineの型紙を出し、対応するnative curveまたは指定偏差付きsplineとして再読込できる。

### AT-EXP-013 1:1 PDF

A4/A3、縦横自動、5mm重なり、複数ページ、基準100mm線、ページ番号を検査する。
PDF上の基準線が100mmであることを座標値から検査する。

### AT-EXP-014 選んだものだけの文書

選んだ Entity(種類を問わない)と、それを作るのに要る上流の Feature だけを持つ kcd2 を出す。
上流は inputEntityIds と作り方の中身が指す id の両方からたどる(押し出しの相手、線の作業平面など)。
選んでいないが参照されているものは残し、名前で知らせる(EXP-S003)。無関係なものは入らない。
出来た kcd2 は Validate を通り、開き直すと選んだものが作り方ごと作り直される。
何も選んでいなければ EXP-S001、選んだものが文書に無ければ EXP-S002 で断る。

## 12. 性能・安定性

### AT-PER-001 UI応答

100msを超える評価中にevent loop heartbeatが250ms以内で更新され、Cancel操作を受け付ける。

### AT-PER-002 古い評価破棄

長い近似中にsource revisionを更新する。古い結果を採用せず、新revisionだけを表示する。

### AT-PER-003 大規模文書

1000 Wire、100 Feature、20 Partを含むfixtureの読込、選択、保存を行う。
Windows基準機で読込5秒、保存5秒を目標とし、超過は計測値付きWarningにする。メモリ使用は1GB未満。

### AT-PER-004 連続操作

作図、Undo、近似再計算、保存を500操作繰り返し、クラッシュ、Entity増殖、ID衝突、handle leakを起こさない。

## 13. Windows検証ゲート

各作業パッケージ:

```powershell
.\\scripts\\check-v2.ps1
```

正式切替後:

```powershell
.\\scripts\\check.ps1
.\\out\\build\\windows-release\\kachakacha_cad.exe --self-test
```

最終切替はさらに次を必須とする。

1. Release build。
2. 全V2 test。
3. `--self-test`。
4. ER1/ER2 scenario出力再検査。
5. 配布zipを空の作業フォルダへ展開。
6. exeをコマンドから起動して初期画面表示を確認。
7. sample `.kcd` を開く。
8. 30% STEPと1:1 PDFを出す。
9. アプリを通常終了。
10. CI成功後にmainへmerge。

## 14. 画像付きマニュアル

正式切替前に `docs/manual.html` をV2専用に書き直し、全利用者向けコマンドを1回以上説明する。

- 各章に実画面画像を付ける。
- 画像はV2 Release buildから自動または再現手順付きで生成する。
- 旧UI画像を混ぜない。
- 画像だけでなく、開始条件、操作手順、結果、失敗時の直し方を書く。
- 作図、部品、製作、出力、設定、測定、視点、保存を含める。
- マニュアル生成とリンク切れをself-testに含める。
