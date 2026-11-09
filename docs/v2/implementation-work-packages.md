# Wire-first V2 実装パッケージ

## 1. この計画の使い方

実装は下表の単位で行う。担当AIは1回に1パッケージだけを所有する。状態の正本はこの表である。

| ID | 状態 | 依存 | 担当領域 |
| --- | --- | --- | --- |
| WP-00 | 完了(`25d9982`、文書監査、Windows 18試験、self-test) | なし | 規範文書と受入基準の凍結 |
| WP-01 | 完了(`codex/v2-wp01-build-scaffold`、v2試験4本、Windows全試験) | WP-00 | V2 build scaffold、依存、ライセンス |
| WP-02 | 完了(Codex、2026-11-04段階12。形状転送KER-X001を台帳へ整合、診断台帳13例成功) | WP-01 | ID、Diagnostic、Tolerance、基本値型 |
| WP-03 | 重点検証済み(Codex、2026-11-04段階3。型付きUUIDと依存順序の一度検証。document/構造試験成功、1000線確定42.54→13.27 ms。全体検証は後段) | WP-02 | Document、Feature DAG、Command、Undo |
| WP-04 | 今回の追加修正は全体検証済み(Codex、2026-11-07、481c845e5の更新4。PlanarWireOffsetの開いた直線/円弧鎖の接続保持とコア回帰のみ。修正前4件の不合格を保全し、修正後40コア例・関連6群・既存UI2件・添付84c/85b/86の中立断面→面→厚み→保存再読込が成功。事前9・CTest196・開発版697・配布版697・従来自己試験が成功。元205定義・既存グループを保全。公開API・保存形式・許容差・曲線種類は維持。添付屋根の接合・支持枠と型紙、WP全体の残件は未完成) | WP-02 | Curve/Wire/Chain、編集、数式 |
| WP-05 | 完了(v2_json / v2_zip / v2_document_file / v2_atomic_file。手書きのJSONとZIP。1バイト書き換えの総当たりも通す) | WP-03, WP-04 | `.kcd2` JSON/ZIP保存 |
| WP-06 | 今回の支持範囲端の丸めへの限定実装は関連回帰確認済み(Codex、コア20件・関連UI10件・実UI361。同じ保存面から整形/オフセット/厚み/履歴/KCD2/STEP/型紙3形式が成功。微小な範囲外を含む偏差上限を評価し、実際の超過・不正境界の拒否、元境界/穴/向きと周期面の既存処理、許容差・公開API・保存形式を維持。限定実装の所有は終了、全体・配布版ゲートは待ちで、完成配布版とは扱わない。全車体製作とWP全体の残件は未完了)。過去の記録: 今回の切り分けた曲面の有効UV支持範囲処理は全体・配布版検証済み(Codex、2026-11-09、61e958af5の公開更新6。既存SurfaceRefit/Offsetの非公開処理と対応回帰のみ。コア19・関連UI10・実UI344d/345cの整形/直接オフセット/型紙3形式/厚み/履歴/KCD2/STEPが成功。使用範囲内の真の折れと不正境界の拒否、元データと穴・向き、周期面の既存処理、許容差・公開API・保存形式を維持。事前9・CTest197（skip0）・開発/配布UI各722・通常起動・従来自己試験とGitHubの対象コミット/最新タグ/ZIPサイズ/SHA256が一致。限定所有は完了。全車体の製作モデル・全型紙・実工作とWP全体の残件は未完了)。過去の完了記録: 今回の追加修正は全体・配布版検証済み(Codex、2026-11-07、4f260c258の更新5。今回の所有は既存SurfaceRefitの私有境界許容差と回帰のみ。修正前の2不合格を保全し、修正後は反転/回転・複数穴・実際の端点誤差拒否・球の極・実曲面の再構成→押し出し→STEPを含む17件が成功。実UI102/103/104bで再構成・保存再読込・方向押し出し・単独STEP出力が成功し、元219定義と原本を保全。最大境界許容差17.722651301→0.000056905mm。形状の偏差上限0.000073mm。公開API・保存形式・出力検査は変更しない。事前9・CTest196・開発版697・配布版697・通常起動・従来自己試験を通して更新5を公開し、GitHubの対象コミット・サイズ・SHA256も一致。前頭屋根の一定厚み/実製作・全模型は未完成。詳細はrefactoring-plan。従来の追加修正は全体検証済み: Codex、2026-11-07、0146cddafの更新3。既存オフセット面とC0折れ位置での製作境界採取の私有処理のみ。4本の新コア回帰・関連7群・回転/移動・実UI82/83と再読込が成功。2回の事前不合格を保全し、順番実行の事前9件・CTest196件・開発版697件・配布版697件・従来自己試験が成功。接合可能な折れた平面帯から穴・切り欠きを保つ中立面を通常UIで作れる。公開API・保存形式と既存の誤差・点数・深さ上限は維持。既存の全体検証済み記録: 無効面拒否と転送診断は2026-11-06の4ef46c1dd更新4。帯の対応を保つ生成修正はWP-10。f53a24e5aまでの再近似も検証済み。WP全体の既存残件と添付車体の製作用屋根・近似・型紙の未完成は維持する) | WP-03, WP-04 | OCCT evaluator、GuideSurface |
| WP-07 | 今回の切断平面の体積基準点の限定実装は関連回帰を確認済み(Codex、2026-11-09。元の局所点を切断平面へ投影し、元形状・両側・既存の準備済み形状を同じ点で積分する。分割・形状・判定値・許容差と他の測定の既定処理、公開API・保存形式を維持。修正前は追加回帰だけ失敗し既存8件は成功。修正後はコア9件・HP-SHのUI7件と実UI382の左右/二領域の面取得・平板押し出し・保存/履歴/KCD2/STEP/型紙3形式が成功。3位置×反転×回転/移動を確認。限定所有は終了し全体・配布版ゲートは未実行。実UI382の左断面の境界標本上限は別件として保全し調査中)。今回のBoolean体積基準点の限定実装は関連回帰を確認済み(Codex、2026-11-09。非公開なOffsetVolumeの比較基準点指定だけを追加し、Booleanの入力と結果へ同じ局所点を渡す。既定の他測定、形状、判定値、安全検査、公開API・保存形式を維持。修正前は追加した5層回帰だけ失敗し既存18件は成功。修正後はコア19件・関連UI5件・実UI367の結合/保存/Undo/Redo/KCD2/STEPが成功。逆順・回転・(1000,-2000,3000) mmへの移動を含む。さらに遠い配置の品質検査と実UI365の分割体積差は別件として証拠を保全。限定実装所有は終了し、支持範囲端の修正と合わせた全体・配布版ゲートは未実行。更新6までの公開検証は維持)。過去の記録: 今回の単一ソリッドのコンテナ処理修正は全体・配布版検証済み(Codex、2026-11-09、86a0c3b2aの公開更新2。重点Boolean18件・実UI232/233・native材料/穴/元70定義の保全に加え、事前9・CTest197（skip0）・開発/配布UI各719・通常Windows起動・従来自己試験が成功。GitHubのタグ・対象コミット・最新タグ・ZIPサイズ/SHA256も一致。公開API・保存形式・UI仕様・安全検査と許容差は維持。限定所有は完了。車体全体の製作近似・全型紙とWP全体の残件は未完了)。過去の記録: 結合済み曲面の平面分割と私有STEP実範囲照合は全体・配布版検証済み(Codex、cf7a75faaの更新9、事前9・CTest197・開発UI714・配布UI714・通常起動・従来自己試験・GitHub同一ZIP照合が成功。この限定所有は完了。車体製作モデルと全型紙、WP全体は未完了)。過去の記録: 2026-11-08の結合済み曲面の平面分割と私有STEP範囲照合は重点検証済み(5607e0a75、native回帰・実UI186/187/188b/189b/190。全体・配布版ゲート待ちで、この限定実装の所有は終了)。過去の記録: 追加の隣接曲面ソリッドの足す処理は進行中(Codex、2026-11-08。実UI181の180秒上限を保全し、既存Booleanの私有計算の計測・高速化・回帰だけを限定所有する。公開API・保存形式・安全検査は維持)。 今回の平面終端への閉鎖修正は全体・配布版検証済み(Codex、2026-11-08、5372c60a2の更新1。既存BuildExtrudeToFaceの非公開処理と回帰に限定。元の曲面と3D境界を保ち、終端平面に境界が接しても正の体積を閉じられる。複数穴・円筒・斜面/斜め方向・移動反転・有限穴拒否・添付左右を含む8回帰と関連7群、実UI109/110/111bを照合。事前9・CTest197・開発UI701・配布UI701・通常起動・従来自己試験が成功し、公開ZIPのコミット/サイズ/SHA256も一致。密な曲面同士のBoolean生成とWP全体の残件は未修正のまま継続。公開API・保存形式は増やさず、安全検査を維持する。公開更新5の実UI106で密な曲面部品の接合が180秒停止。単独の両部品は出力品質検査に通り、重なる内側点があるのに素のnative共通演算は空になる。元形状・穴・向き・誤差予算・体積整合・閉鎖/自己交差の安全検査、編集上の操作単位と公開API/保存形式を維持して修正する。詳細はrefactoring-plan。前回の厚み修正は全体検証済み: Codex、2026-11-06、90882f712の修正をf53a24e5aの更新3で検証。内部停留点・方向別の速度・C0の片側法線を修正。品質31件と全体ゲートが成功し、元面とnative安全検査を維持。添付屋根の真に法線が一意でない角の一定厚みは未対応で、強制成功にはしない) | WP-06 | Part作成、押し出し、Boolean、Subshape |
| WP-08 | 今回の軸に近い平面の姿勢変換と既存配置欄の保持精度/X・Y・Z識別の限定修正は全体・配布版検証済み(Codex、2026-11-09、f78387e85の公開更新3。事前9件、CTest197件（skip0）、開発版・配布版UI各720件、通常Windows起動と従来自己試験が成功。GitHubのタグ・対象コミット・最新タグ・ZIP 76,019,288バイトとSHA256も照合した。公開API・保存形式・元の曲線/穴/座標・安全拒否は維持。限定所有は完了。全製作モデル・全型紙とWP全体の残件は未完了)。今回の着手・重点記録: 重点検証済み(Codex、2026-11-09。軸に近い平面の私有姿勢変換と既存配置欄の入力精度・X/Y/Z識別を修正。回帰の修正前失敗/修正後成功、関連5試験群、実UI244/245/248b/251で原寸出力・元70定義・配置・一枚の平板への接合と履歴/STEPを照合。今回の限定実装の所有は終了。UI720件の全体・配布版検証と公開は後段、公開済みは更新2。DXF外部読み込みの不正は別件としてWP-11へ移り、全形式・全製作モデルの完了とは扱わない)。過去の記録: 今回の出力形式保持と設定表F2の限定UI修正は全体・配布版検証済み(Codex、2026-11-09、94dfb4405の更新1。重点2件・関連28件・構造検査・実UI214bに加え、事前9・CTest197（skip0）・開発/配布UI各719・通常Windows起動・従来自己試験が成功。GitHubのタグ・対象コミット・最新タグ・ZIPサイズとSHA256も一致。元モデルと明示対象の範囲、出力内容を保ち、公開API・保存形式・幾何計算は変更しない。限定所有は完了。車体全体の製作モデル・全型紙とWP全体の残件は未完了)。過去の着手記録: 進行中(Codex、2026-11-08。出力後の暗黙対象数更新で選択形式までSTLへ戻る経路と、設定表F2が部品名変更へ渡る経路。V2ExportDock・既存ParameterDockの私有UIと回帰だけを限定所有し、出力内容・公開API・保存形式・幾何計算を変えない。受入はrefactoring-plan参照)。過去の記録: 独立フェイス抽出と型紙出力の私有UI接続は全体・配布版検証済み(Codex、cf7a75faaの更新9、関連27件・実UI191b/192/193、事前9・CTest197・開発UI714・配布UI714・通常起動・従来自己試験・GitHub同一ZIP照合が成功。この限定所有は完了。製作モデルと全型紙、WP全体は未完了)。過去の着手記録: 進行中(Codex、2026-11-08。独立フェイス抽出の不要な全体再計算と一時native所有、製作部材を選んだ原寸出力の対象解決、共通一覧の実入力回帰という既存Qt UIの私有接続だけを限定所有。公開コアAPI・保存形式・幾何精度を変更せず、WP-07へ同時着手しない。受入と実UI186/188の再現はrefactoring-plan参照)。過去の記録: 今回の限定所有は全体・配布版検証済み(Codex、2026-11-08、96af889dfの更新6を公開。シェル/平面分割の確定中の下見寿命、1再計算内の両側共有と未使用形状解放、削除済みnative対応表整理、窓終了時の解放。追加4回帰と関連163件、実添付由来160/161で分割直後・保存/実Ctrl+Z/Y後の両側STEPと片側削除後の再読込/STEPが成功。元の定義・ID・原本を保持。事前9、CTest197、開発UI710、配布UI710、通常起動、従来自己試験が成功し、GitHubのコミット/タグ/最新タグ/ZIPサイズ/SHA256も一致。38399ca8bの作業平面数値欄検証も含む。限定所有は完了。製作近似と全型紙、WP全体の残件は未完了)。過去の着手記録: 進行中(Codex、2026-11-08。今回の所有は削除済み部品/形状ガイドの私有native対応表整理（片側削除後の再読込JOB-001を再現）、既存シェル/平面分割の確定中の下見寿命と、1回の再計算内だけで両側を共有して残りを解放する私有UI接続・実UI回帰に限定。添付由来159bは保存/実Ctrl+Z/Y/両側STEPが成功したがKER-M002通知が残る。確定直後、取消/再実行、保存再読込で実形状・体積・周辺とnative所有数を照合し、検査・公開コアAPI・保存形式は変更しない。作業平面欄の識別名38399ca8bは重点検証済みで次の全体ゲートに含める)。直前の限定所有: 進行中(Codex、2026-11-08。今回の所有は既存の作業平面の通過点・法線・横方向欄への一般的な識別名と実入力回帰に限定する。無効値の拒否、有効値の確定、実正対、保存再読込を通常のUI入力で確認し、代入APIや視点設定で操作を代替しない。公開API・保存形式・幾何計算を変更せず、WP-07へ同時着手しない。任意ギズモ再生の追加は範囲外)。直前の完了記録: 今回の部品追加/取消の限定修正は全体・配布版検証済み(Codex、2026-11-08、da8957fe0の更新5を公開。事前9、CTest197、開発UI705、配布UI705、通常Windows起動、従来自己試験が成功し、GitHubの対象・最新タグ・ZIPサイズ・SHA256も一致。公開配布版の実UI156でも元139の保存→Ctrl+Z/Y→STEPが成功し、Undo2.0709秒、Redo11.9400秒、元の全定義とIDを維持。限定所有は完了。製作用近似と全型紙は未完成)。過去の記録: 進行中(Codex、2026-11-08。現在の所有はこのWPの部品追加・取消で不要な全体再計算を避ける私有UI接続と回帰に限定する。実UI140bの保存後Undoは83→82部品へ戻ったが66.35秒かかり、続くRedo中に180秒上限へ達した。共通の元形状・許容差・依存が変わらない追加/削除だけを再利用候補とし、変更した元形状・面・製作モデル・不明な状態は従来の全体再計算へ戻す。公開コアAPI・保存形式・形状精度を変えず、WP-07の製品幾何変更へ同時着手しない。前回の保存後Undo維持は全体・配布版検証済み(Codex、2026-11-08、2d45f9234の更新4)。追加回帰と関連55件、実UI146の保存→Ctrl+Z/Y→STEP、事前9、CTest197、開発UI703、配布UI703、通常Windows起動と従来自己試験が成功し、GitHubのコミット・ZIPサイズ・SHA256も一致した。長い履歴の速度問題と模型・製作用近似・全型紙は未完成のまま保持する。過去の記録: 前回の非表示入力の限定修正は全体・配布版検証済み(Codex、2026-11-08、4dd66a4c9の更新3。非表示で明示選択された元線だけを私有計算シーンへ補い、Auto判定と役割表示を接続。可視入力ではシーン複製なし。実添付由来の134で元287定義と表示を保全。追加1件を含む7件、事前9、CTest197、開発UI702、配布UI702、通常起動、従来自己試験が成功。公開ZIPのコミット・サイズ・SHA256も一致。今回のWP-08所有は完了し、次はWP-07の既存曲面立体化・非公開調査へ戻す。過去の記録: 2026-11-08。所有はこのWPの曲線から近似面UI・自動判定の入力解決と回帰だけに限定した。WP-07の密な曲面の読み取り専用調査はいったん止める。添付由来の実UI132で、左一覧から選んだ非表示の元線が自動判定で解決できず、確定不能になる経路を再現した。選択した線だけを計算用コピーで解決し、表示状態・入力形状・公開API・保存形式を変えずに修正する。既存の全体検証記録と未完了事項は保持する。前回記録: 2026-11-07。任意取消リプレイの限定確認は重点5件と実添付UI108で成功し、現在の所有をWP-07の既存Boolean核・非公開解析の調査へ戻す。UI108は624ms生存中の計算を見えるボタンから取消し、線作成/Undo後の225項目と元ファイルを維持。追加試験バイナリは全体検証済み更新7ではない。小部品のBoolean4接続は548c541fdで修正し、事前9・CTest196・UI701×2・従来自己試験・通常起動を通して更新7を公開済み。密な曲面の幾何生成は未修正。以前の私有UI/Viewport表示修正、e916b30c7のchooser解除・大量一覧選択・生成物分類、9c1018744の折り曲げ帯の所有者に従う非表示・親グループ・局所表示・選択ズームも全体検証済み。過去不合格とWP全体の残件は維持し、36mm車体の屋根・製作用近似は未完成) | WP-03, WP-04 | Qt shell、作図、snap、grid、測定、view |
| WP-09 | 今回の穴のない曲面の材料境界provider・方式分岐の限定修正は全体・配布版検証済み(Codex、2026-11-09、83b6248a6の更新5。コア15件、関連UI41件、同じ保存中立面の実UI303で型紙3形式・輪郭・面・厚み・保存/Undo/Redo・KCD2/STEP・再読込が成功。完全矩形の帯近似の形/姿勢/偏差・従来経路を保ち、穴/切り欠きと不正領域拒否を維持。途中の性能退行は計測して修正し、事前9件、CTest197件（skip0）、開発/配布UI各721件、通常Windows起動と従来自己試験が成功。正式publish後にGitHubのタグ・対象コミット・最新タグ・ZIPサイズとSHA256も照合した。公開API・保存形式・元形状/穴/寸法・許容差は維持。限定所有は完了。全製作モデル/全型紙・細分化の軽量化とWP全体の残件は未完了)。過去の完了記録: 今回のepsilon付近の材料境界切断・接続の限定修正は全体・配布版検証済み(Codex、2026-11-09、5be3b4998の公開更新4。境界/穴の13件、元面/近似外面/中立面のU/V、実UI270d/281の型紙・厚み・保存/Undo/Redo・KCD2/STEP・再読込に加え、事前9件、CTest197件（skip0）、開発/配布UI各720件、通常Windows起動と従来自己試験が成功。GitHubのタグ・対象コミット・最新タグ・ZIPサイズとSHA256も照合済み。公開API・保存形式・元面/穴/寸法と不正入力の拒否は維持。限定所有は完了。生成物の細分化/軽量化・全製作モデル/全型紙とWP全体の残件は未完了)。今回の着手・重点記録: 進行中(Codex、2026-11-09。今回の所有はオフセットした有効曲面のUV材料境界を近似へ渡せない実UI270/270b/270cの限定調査・修正。既存provider・材料領域の非公開処理と回帰だけを対象にし、公開API・保存形式・元面/穴/寸法と不正領域の拒否を維持。受入と切り分け範囲はrefactoring-plan参照。公開更新3の全体・配布版ゲートは完了済みで、この新件と全車体製作/全型紙は未完了)。従来のコアゲート完了(v2_fabrication / v2_pattern / v2_panel_strategy / v2_opening_clip / v2_assembly / v2_closed_loop / v2_freeze_state) | WP-06, WP-07 | 製作近似コア、型紙、組立solver |
| WP-10 | 今回の独立KCD2合成先の私有検証修正は全体・配布版検証済み(Codex、2026-11-08、bc1ae78ceの更新10。事前9・CTest197・開発/配布UI各717・通常起動・従来自己試験が成功。実UI197dで19層を合成・再読込し、元形状とのnative体積・実範囲・交差による形状差は全て0。実UI201の19部品STEPとGitHub同一ZIP照合も成功。原本の生データ・既存定義/ID/資産と実変更拒否を維持し、公開API・保存形式・worker転送形式・幾何計算は変更しない。限定所有は完了。車体の全製作モデル・全型紙とWP全体は未完成)。過去の記録: 追加の独立出力と既存KCD2の合成UIは全体・配布版検証済み(Codex、2026-11-08、29879051fの更新7。事前9・CTest197・開発/配布UI各713・通常起動・従来自己試験が成功。実UI168/170とGitHub照合も成功し、この限定所有は完了。AT-EXP-017)。従来は全体検証済み(今回の固定生成のレール対応、Codex、2026-11-06、4ef46c1dd。添付模型の左右16面の生成・保存再読込、関連UI97件に続き、事前9件・CTest195件・UI692件×2・従来版自己試験・通常起動が成功。更新4を公開。部材3の0.1mm厚みは成功、部材1は安全拒否で製作モデルは未完成。現在の所有は同じ私有生成接続のみ。Claudeの既存作業記録とWP全体の残件は維持し、公開APIは変更しない) | WP-08, WP-09 | 部品/製作/出力UI接続 |
| WP-11 | 今回の曲線付きDXFの版・mm単位・必須サブクラスと固有ハンドルの限定修正は全体・配布版検証済み(Codex、2026-11-09、f78387e85の公開更新3。事前9件、CTest197件（skip0）、開発版・配布版UI各720件、通常Windows起動と従来自己試験が成功。GitHubのタグ・対象コミット・最新タグ・ZIP 76,019,288バイトとSHA256も照合した。公開API・保存形式・元の曲線/穴/座標・安全拒否は維持。限定所有は完了。全製作モデル・全型紙とWP全体の残件は未完了)。今回の着手・重点記録: 進行中(Codex、2026-11-09。実UI252の曲線付きDXFが外部読み込み器でAcDbSpline欠落として拒否される経路。既存DXF writerと出力回帰だけを限定所有し、曲線種・制御点/ノット/方向/座標/レイヤー・原寸・非平面拒否を維持。版とmm単位・必須構造を公式仕様と外部読み込みで照合する。公開API・保存形式・製品依存は変更しない。受入はrefactoring-plan参照)。過去の記録: 重点検証済み(Codex、2026-11-07。今回の限定調査は終了・製品コード変更なし。実UI98とnative回帰で前頭屋根の厳密厚み拒否を再現。平面の境界積分への切替も曲面の1e-8精度を満たさず不採用。元曲面の内部境界で両側法線の角度差約0.051754radが残ることを段階的に確認。既存31件＋表裏/方向反転・元面保持の拒否回帰1件が成功。前頭屋根の厚みと模型は未完成で、修正完了や新配布版とは扱わない。公開済み段階15と更新4までの全体検証記録、WP全体の未完了事項は維持する。詳細はrefactoring-plan参照) | WP-05, WP-07, WP-09 | STL/STEP/SVG/DXF/PDFと保存統合 |
| WP-12 | 進行中(Claude。ER1/ER2 の受入モデル、図つきマニュアル、見本 .kcd2、文書を開く道、配布 script は完了。実機での配布zip確認と切替が残り) | WP-01..11 | 全受入、マニュアル、配布、切替 |
| WP-13 | 保留(オーナー指示待ち。「V2が満足いくものになるまでV1は消すな。満足したら指示する」2026-09-09)。準備だけ進める | WP-12 | 旧実装削除と最終整理 |
| WP-14 | 重点検証済み(Codex、2026-11-04段階2。選択・候補照合索引、非表示測定遅延。UI13経路成功、ホバー中央値7.07→0.75 ms。全体検証は後段) | WP-08 | 統合UI/UX、選択、カーソル、スナップ、ToolSession、プロパティ、プレビュー、画面復元 |

状態は `未着手 / 進行中(担当、branch、date) / blocked(理由、試行) / 完了(commit、試験)` のいずれか。
担当開始時と完了時は、この表だけを変更する独立コミットを先にpushする。

## 2. 共通Definition of Done

各WPは次を全て満たすまで完了にしない。

1. 規範文書に対応する実装がある。
2. 対応する受入試験またはそのWPで実行可能な下位試験がある。
3. successだけでなくinvalid/degenerate/cancel/retryを試験する。
4. public APIに表示名参照、Qt型、OCCT型が漏れていない。
5. 例外でアプリまたはtest runner全体が落ちない。
6. 変更した全ファイルをformatし、警告0でbuildする。
7. `scripts/check-v2.ps1` の実行可能範囲が通る。
8. Windows/Qt/OCCT変更はWindows実機またはCIで通る。
9. 仕様との差分が0、または採用済みADRで説明されている。
10. 作業ブランチをpushし、commit hash、試験、未実施項目を報告する。

「後で接続する空ボタン」「常に固定値を返す仮実装」「見た目だけのmesh」「TODOで逃がした必須処理」は
成果物として数えない。

## 3. コーディング規則

### 3.1 新規コードの大きさ

- 新規 `.cpp` は原則1500行以下。
- 新規 public headerは500行以下。
- V2のMainWindow shellは400行以下。
- 1関数100行超は分割する。幾何アルゴリズムで必要な場合は関数冒頭に理由を1行記す。
- `Utils.cpp`、`Helpers.cpp` のような責務不明ファイルを作らない。
- 同じ分岐または変換を3回書かない。
- UI toolは共通 `ToolController` state machineへ登録し、巨大enum比較を増やさない。

### 3.2 禁止

- 表示名でEntityを検索して依存参照する。
- UIクラス内で幾何計算する。
- core public APIへQString/QColor/QPoint/TopoDS_Shapeを入れる。
- preview meshをPartの正本にする。
- 最寄り要素へ壊れた参照を自動付け替えする。
- `catch (...) { return success; }`。
- invalid inputに空Entityを返す。
- 失敗したFeatureの一部outputだけをDocumentへ残す。
- テストを通すために許容差をその場で拡大する。
- unordered containerの順序を保存結果、自動候補順、番号へ使う。
- 仕様外の機能を「ついで」に追加する。
- 他WPが所有するpublic headerを相談なしに変更する。
- 旧実装へV2分岐を追加して一時的な二重経路を恒久化する。

### 3.3 必須

- エラーcodeを安定させ、日本語文とは分離する。
- 許容差を `GeometryTolerance` から受け取る。
- ID generator、clock、filesystem failure、cancelをtestで注入可能にする。
- public API変更にはcompile testまたは直接unit testを付ける。
- expensive operationはCancellationTokenを確認する。
- source revisionとresult revisionを照合する。
- test fixtureへ実数値と由来を記す。
- 1コミット1目的。機械的移動と挙動変更を分ける。

## 4. WP-00 規範文書

### Deliverables

- `docs/v2/README.md`
- `docs/v2/product-contract.md`
- `docs/v2/architecture-and-data.md`
- `docs/v2/public-api-contract.md`
- `docs/v2/kcd2-format.md`
- `docs/v2/geometry-contract.md`
- `docs/v2/fabrication-contract.md`
- `docs/v2/ui-workflows.md`
- `docs/v2/command-catalog.md`
- `docs/v2/acceptance-tests.md`
- `docs/v2/traceability-matrix.md`
- `docs/v2/implementation-work-packages.md`
- `docs/v2/agent-master-prompt.md`
- `docs/v2/integration-lead-prompt.md`
- ADR 0026
- `AGENTS.md` と `CLAUDE.md` からV2入口へのリンク

### 完了確認

- 用語検索で利用者向け `Surface/Plate/Body` の扱いが矛盾しない。
- 全PRD必須領域にAT試験がある。
- WP依存にcycleがない。
- 旧文書よりV2が優先されることが明記される。
- オーナー未確認の重大分岐が残っていない。

## 5. WP-01 Build scaffold

### 所有

- root `CMakeLists.txt`
- 依存取得方法の決定(ADR 0027。`vcpkg.json` は追加しない — 理由はADR参照)
- `scripts/check-v2.ps1`, `scripts/check-v2.sh`
- `.github/workflows/windows-build.yml`(V2ブランチでCIを回し、`kachakacha_cad_next.exe` を配布zipへ入れる)
- V2 targetだけの空でない最小source
- V2試験の土台(`src/next/kachakacha/base/TestHarness.h`。1件失敗で残りを止めない)
- `docs/licensing-audit.md`

### 実装

- `kachakacha_v2_core`、`kachakacha_v2_occt`、`kachakacha_cad_next` を追加。
- 現行targetとtestを壊さない。
- 依存の取り方をADR 0027で決める。`vcpkg.json` を置くとvcpkgがマニフェストモードへ
  切り替わり、Qt/OCCTの解決が壊れるため、初回は追加しない。
- test-only以外の新依存を追加しない。
- `check-v2` はconfigure/build/ctestを行い、test未登録なら失敗する。

### Gate(一次)

- 現行 `check.ps1`。
- `check-v2.ps1`。
- `AT-ARC-001` 依存境界(V2 coreがQt/OCCTをincludeしない)。
- `AT-ARC-005` コード衛生(ファイル1500行・関数100行・未完了マーカー)。
- 試験土台が「1件失敗で残りを停止しない」ことを、土台自身の試験で示す。
- ライセンス表更新。
- next exeが空ウィンドウではなく、versionと「V2準備中」を出す最小shell。これは製品機能完成には数えない。

## 6. WP-02 Foundation

### 所有

- `base/Ids.*`
- `base/Diagnostic.*`
- `geometry/GeometryTolerance.*`
- `geometry/Units.*`
- `geometry/Vector*` の移植/整理
- test ID generator

### API

- strong UUID wrappers。
- parse/format/order/hash。
- Diagnostic value types。
- finite number validation。
- document unit conversion。
- global default tolerance factory。

### Gate

- UUID roundtrip、invalid parse、fixed generator、stable ordering。
- unit dimensional mismatch。
- tolerance model diagonal boundary。
- NaN/Infinity rejection。

## 7. WP-03 Document/DAG/Command

### 所有

- `domain/Entity.*`
- `domain/Feature.*`
- `domain/FabricationSettings.*`
- `document/Document.*`
- `document/FeatureGraph.*`
- `document/DocumentCommand.*`
- `document/UndoStack.*`
- `document/Group.*`

### API契約

- Entity/Featureのadd/update/removeはCommandだけ。
- typed refsとBrokenReference。
- DAG validation、affected subgraph、revision。
- transaction candidate/commit/rollback。
- active groupとderived group。
- immutable `DocumentSnapshot`。

### Gate

- AT-DOC-001から005。
- 100回Undo/Redo。
- randomized DAG property test。seed固定、cycle拒否、順序決定性。

## 8. WP-04 Wire/Chain

### 所有

- `geometry/CurveSegment.*`
- `geometry/Wire3d.*`
- `geometry/WireChain.*`
- `geometry/Intersections.*`
- `modeling/WireCommands.*`
- `io/NumericExpression.*` の移植

### 実装順

1. 値型とevaluation。
2. arc length/closest/split。
3. endpoint graph。
4. deterministic chain。
5. intersection。
6. trim/extend。
7. join/G1/G2。
8. arc constructors。
9. expression。

### Gate

- AT-WIR-001から008。
- 既存Wire testsの同等ケースを移植。
- Polyline化による正本変換0件。
- branch/gap/degenerateのcode固定。

## 9. WP-05 `.kcd2`

### 所有

- `io/KcdArchive.*`
- `io/DocumentJson.*`
- `io/SchemaV2.*`
- save/open tests

### 実装

- ZIP container。
- `document.json`。
- frozen/imported BREP asset index。
- canonical JSON writer。配列表示順は維持、object keyは安定順。
- atomic save、reopen validation、backup。
- old format detection。

### Gate

- AT-EXP-001から004。
- failure injection各段階。
- zip-slip pathを拒否。
- 1GB超、異常圧縮率、重複entryに上限と拒否。
- JSON recursion depthと配列件数に安全上限。

## 10. WP-06 OCCT/GuideSurface

### 所有

- `kernel/OcctGeometryEvaluator.*`
- `kernel/OcctCurveConversion.*`
- `kernel/OcctGuideSurface.*`
- `modeling/GuideSurfaceFeature.*`
- kernel cache

### 実装

- core曲線とOCCT edge/wireの双方向変換。
- Planar/Ruled/Loft/Guided/Gordon/BoundaryFill/OffsetGuide。
- input curve deviation validation。
- cancellation checkpoints。
- cache key = document revision + feature revision + tolerance。
- TopoDS objectをcoreへ返さない。

### Gate

- AT-GEO-001から007。
- invalid inputsでOCCT例外をDiagnostic化。
- 同じ入力のSubshapeKey順が同じ。
- leak/handle lifetime smoke。

## 11. WP-07 Part/Extrude/Boolean

2026-11-08限定所有（分割の別件）: 進行中（Codex）。更新8の実UI186で、結合済みの曲面部品を
Z27.15の平面で分割すると両側の体積和が一致せず下見を拒否された。`OcctShellSplit.cpp`の
私有分割と対応回帰だけを担当する。入力・両側の幾何と体積和、閉鎖性、精度、公開API、保存形式を維持し、
同じ実UI経路の保存/履歴/再読込/STEPと、非接触・穴・回転・複数領域を確認する。
下記の更新8の結合性能修正の限定所有は完了のままであり、同時に他WPへ着手しない。
分割の同じ検証で、STEP再読込のUV外接領域が切取られた実形状より大きくなり誤拒否する経路も確認した。
必要な私有ノット分割補助と`OcctOutput.cpp`内の再読込範囲照合を限定範囲へ含める。
nativeの切取られた形状から範囲を計測し、既存閾値と出力内容・公開APIは維持する。WP-11全体の所有は変えない。

2026-11-08限定所有: 完了（Codex、fda71933bの更新8で全体・配布版検証と公開を完了）。更新7の実UI181で、添付由来の隣接する曲面ソリッド2個の
「足す」が180秒上限に達した。既存Booleanの私有計算の段階計時、限定した高速化と回帰だけを担当する。
入力・結果の幾何、閉鎖性・体積・自己交差・連結性の検査、精度・時間上限、公開APIと保存形式は維持する。
同じUI経路と保存/履歴/再読込/STEP、接触・非接触・重複・回転・複数工具で確認する。
段階計測で結合と結果整理は計約2秒、呼出先の立体検査が診断60秒上限へ達した。
PartValidation相当の検査を置くOcctSolidExport.cppの私有解析分割とOcctIntersectionPartition.cppも
限定所有へ含む。出力API・出力内容・安全判定は維持し、出力機能全体の所有は変更しない。
WP-10の限定所有は完了し、他WPへ同時着手しない。従来の進捗記録と未完了項目は保全する。
既存ノットでの解析分割を先に試し、同じ体積・外形・閉鎖性・自己交差の基準で採否を判断する。
入力のnative回帰2例、実UI181/184b/185、CTest197/197、UI713/713×開発・配布、通常起動、
従来版自己診断と公開ZIPのリモート照合が成功した。この私有性能修正だけを完了とし、WP-07全体の完了判定は変えない。

### 所有

- `modeling/PartFeature.*`
- `modeling/ExtrudeFeature.*`
- `modeling/BooleanFeature.*`
- `kernel/OcctPartBuilder.*`
- `kernel/SubshapeNaming.*`
- `kernel/PartValidation.*`

### 実装順

1. profile faceとholes。
2. basic extrude。
3. semantic subshape naming。
4. wire cage planar shell。
5. GuideSurface trim patches。
6. shell sew/solid validation。
7. ToTarget。
8. add/cut。
9. disconnected result preview。

### Gate

- AT-GEO-010から013。
- AT-EXT-001から008。
- BRepCheck valid、volume、connectedness。
- invalid shapeをDocumentへcommitしない。
- STL/STEPはWP-11まで仮出力しない。

## 12. WP-08 Qt shell/作図/View

### 所有

- `src/apps/cad_next/` のshell、viewport、selection、tool controller、作図panel、測定window、theme。
- core変更は禁止。必要APIは統合担当へ依頼する。

### 構成

```text
MainShell
ModeBar
ModelTree
PropertyPanelHost
OperationGuide
ViewportWidget
SelectionController
SnapController
ViewController
ToolControllerRegistry
CursorNumericInput
DrawingPanel
MeasurementWindow
DisplaySettings
Win95Theme
```

各toolは `IToolController` を実装し、mouse/key eventを巨大switchへ集約しない。

### Gate

- AT-UIX-001から006、008から011の作図部分。
- AT-MEA-001から005のUI表示と作図点作成。
- `command-catalog.md` 8章の完全性ゲート。
- 100/125/150/200%DPI画像。
- View cube quaternion test。
- Shift snap suppression。
- app起動と通常終了。

## 13. WP-09 Fabrication core

### 所有

- `fabrication/SurfaceAnalysis.*`
- `fabrication/PanelCandidate.*`
- `fabrication/PanelOptimizer.*`
- `fabrication/ReliefCut.*`
- `fabrication/OpeningMapping.*`
- `fabrication/Develop.*`
- `fabrication/AssemblySolver.*`
- `fabrication/FoldState.*`
- `kernel/OcctFabricationGeometryProvider.*`

### 実装順

1. Planar/Cylindrical/Conical解析解。
2. B-Rep cacheからpure fabrication inputを作るprovider。
3. panel metrics。
4. deterministic candidate generation。
5. lexicographic optimizer。
6. manual roles。
7. opening clipping/junction。
8. Straight/V/CurvedV。
9. pattern mapping。
10. hinge/developable assembly。
11. closed loop solver。
12. arbitrary state Wire/Part request。

### Gate

- AT-FAB-001から012、014。
- AT-ARC-006。
- 各候補の数値metrics。
- 0/30/100%寸法不変。
- triangle interpolation実装が存在しないことをreview。
- ER1 fixtureを実行可能なところまで用意。最終判定はWP-12。

## 14. WP-10 Model/Fabrication UI

2026-11-08限定所有: 全体・配布版検証済み（Codex、29879051fの更新7を公開済み）。Output selection UI shellの既存独立出力だけを拡張し、
選択した形状を別の既存KCD2とまとめて別名保存できるようにする。元の両ファイル・履歴・参照・資産を保ち、
新しいUUIDで追加する。対象の取消/不正/変更後の古い読込と原本上書きを拒否する。
公開コアAPI・保存形式・幾何serviceと他WPは変更しない。従来の所有・検証記録は保持する。
事前9件、CTest197件（skipなし）、開発/配布UI各713件、通常Windows起動と従来自己試験が成功。
実UI168/170の既存68部品への追加、元71定義の保持、動作中の読込取消と再選択も成功した。
正式publish後にGitHubの対象・タグ・最新タグ・ZIPサイズとSHA256を照合した。この限定所有は完了。

### 所有

- Part mode panel/controllers。
- GuideSurface role table。
- Fabrication panel/controllers。
- Pattern view。
- Output selection UI shell。

### Gate

- AT-UIX-002、007、009。
- 初心者シナリオ1から7を自動操作。
- operation guide全コマンド。
- invalid previewから修正して同じcommandを再実行可能。
- 部材選択と3D highlightの一致。

## 15. WP-11 出力統合

### 所有

- `kernel/PartExport.*`
- `io/PatternSvg.*`
- `io/PatternDxf.*`
- Qt PDF adapter
- output validation/controller

### 実装

- 同一B-RepからSTEP/STL。
- arbitrary fold state。
- selected Parts/Panels。
- SVG/DXF native curves。
- 1:1 tiled PDF。
- temp write/validate/replace。

### Gate

- AT-EXP-010から013。
- AT-FAB-011、012、014。
- 出力物を別readerで再読込検査。
- 0byte/partial fileを残さない。

## 16. WP-12 全受入と切替準備

### 所有

- cross-package testだけ。
- V2 manual。
- sample `.kcd2`。
- screenshot automation。
- distribution packaging。
- CI workflowのV2 gate。

### Gate

- `acceptance-tests.md` 全項目。
- AT-FAB-013 ER1/ER2。
- 画像付き全機能manual。
- 配布zipから起動。
- 実際にexeを起動してsampleを開き、30% STEPとPDFを作る。
- blocker 0。warningは一覧と理由をownerへ提示。

このWPでは必須機能を新規実装しない。欠損が見つかったら所有WPへ戻す。

## 17. WP-13 旧実装削除

### 前提

- **オーナーの明示の指示。** 2026-09-09 に「V2が満足いくものになるまでV1は消すな。
  満足したら指示する」と指示があった。指示が出るまで、削除は1行も行わない。
  準備(棚卸し・移植・手順の確認)だけを進める。
- WP-12完了。
- ownerがV2画面を比較確認。
- CI green。
- release branch/tag方針確定。

### 削除手順

1. next exeを正式名へ。
2. `.kcd2` 拡張子を同一ZIP+JSON内容の正式 `.kcd` へ切り替える。
3. 旧Project/ProjectScript専用経路を削除。
4. Surface/Plate/Body利用者UIを削除。
5. 旧PartModel/FlatPatternの重複経路を削除。
6. 旧sample/testをV2へ移植または削除。
7. CMake targetとscriptを整理。
8. docsの旧仕様に `V2で置換` bannerを追加。
9. `rg` で旧type名、always-false stub、名前参照を検査。
10. full check、self-test、配布zip、実起動。
11. mainへmerge。

旧削除とV2機能追加を同一commitにしない。

## 18. WP-14 UI/UX統合改訂

### 所有

- `docs/v2/ui-ux-integrated-spec.md` と関連UI規範。
- `src/next/kachakacha/app` のSelectionRef、ToolSession、UI意味状態。Qt/OCCTは入れない。
- `src/apps/cad_next` の画面骨格、カーソル、マウス、右コンテキストプロパティ、各ツールadapter。
- `tests_v2` のUIX試験と `cad_next --self-test` のUI操作ケース。

WP-10が所有する製作、曲げ、出力の幾何serviceとDocumentCommandは変更しない。
WP-14はそれらを共通ToolSessionへ接続するUI adapterだけを所有する。公開契約の変更は
ADR 0028、`public-api-contract.md`、compile testを先に更新する。

### 実装順

1. Phase 0: 規範、ADR、受入試験、追跡表、未達台帳、引継ぎ書。
2. Phase 1: sub-element選択、意味状態、cursor/mouse/snap、ToolSession。
3. Phase 2: 固定4モードと重複棚を統合shellへ置換、context properties、DPI/復元。
4. Phase 3: 作図、編集、測定、形状ガイド、部品、製作用近似、出力を順に移行。
5. Phase 4: ER1/初期ER2 1/87の実用操作と画像付きmanual。

### Gate

- AT-UIX-001から013。
- 既存のAT-WIR、AT-WPL、AT-GEO、AT-EXT、AT-FAB、AT-EXPを退行させない。
- `scripts/check-v2.ps1` と `kachakacha_cad_next.exe --self-test`。
- 100/125/150/200% DPIと1366x768/1920x1080の画像確認。
- UIに `板材` 種別/モード/作成コマンド、空の将来機能、固定4モードを残さない。
- WorkPlane切替/削除後もWireのUUID、SegmentId、3D座標を保持する。

### 分割とpush

Phaseごとに独立コミットとし、Phase 1以降はSelection、ToolSession、shell、個別機能群をさらに分ける。
作業ブランチの中間コミットは対応する重点検証を通してpushする。完成候補は必須の
全体ゲートと配布版自己試験を通す。重点検証を全体検証済みと表示しない。
正式な入口と証拠の扱いは `docs/development/verification-workflow.md`。途中状態は
`docs/v2/handover-ui-ux-2026-09-12.md` へ反映する。

## 19. 複数AIの統合規則

### 19.1 統合担当

1人だけが次を所有する。

- この進捗表。
- root CMake/vcpkgの競合解消。
- public contract headerの最終承認。
- cross-WP API変更。
- merge順とrelease branch。

### 19.2 作業担当

- 最新integration branchから `codex/v2-<wp>-<short-name>` を作る。
- 開始ロックを先にpushする。
- 所有ファイル外を変更しない。
- 他WP APIが必要なら、必要signature、理由、試験を統合担当へ提出する。
- 他担当の未完成コードをcopyして独自版を作らない。
- 作業終了時にbranchをpushする。
- merge後に自分のbranch上だけで続けない。最新integrationへ追従する。

### 19.3 競合予防

- 公開headerは所有WPだけが編集する。
- root CMakeへ各担当が直接sourceを追記せず、WP別 `sources.cmake` をincludeする。
- Diagnostic code registryを1ファイルに集約し、重複をtestする。
- test fixture UUID registryを1ファイルに集約する。
- UI文言はtranslation/catalogへ集約し、同じ操作名を各panelへ直書きしない。
- tolerance、unit、ID、selection ref、output targetを独自定義しない。
- 一時adapterには `V2_MIGRATION_ONLY` と削除WPをコメントし、WP-13 testで残存を検出する。

## 20. 引継ぎ報告形式

```text
WP:
branch:
base commit:
commits:

実装した契約ID:
変更した公開API:
変更ファイル:
追加試験:
実行結果:
Windows/CI:
画面確認:
既知の未完:
仕様との差分:
次担当が必要な情報:
```

`既知の未完: なし` と書けない場合、WP状態を完了にしない。
