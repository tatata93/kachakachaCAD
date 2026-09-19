# UI_LESSONS — 確定した原則と再発防止(短く)

- 3 HTML(作図/部品/製作 2026-09-18)が UI の正本。旧 mock・旧棚・GuideTable 中心 UI より優先。
- 文法は全モード同じ: 道具 → 右ペインが道具専用 → Input Slot → 3D クリック → 次 Slot → 設定 → Preview → Enter。Esc = Cancel。
- 右ペインは「現在の道具」か「選択物のプロパティ」だけ。複数道具の一覧を同時に出さない。
- Tool 中は Slot に合う geometry を優先して拾う(Extrude=領域/平面Face、Revolve軸=線、Loft断面=輪郭、Fillet=Edge、近似=Surface/Face、Boolean=Solid)。Wire が常に勝つ状態は禁止。
- Preview と Commit は同じ input snapshot から。Preview 後に選択を読み直さない。
- Ctrl を暗黙に必須にしない(道具中の素クリックで足す)。
- 「押せるが何も起きない」禁止。backend に無い機能は disabled + 日本語の理由。見た目だけの実装済みにしない。
- 診断は日本語の文が主、コードは補足。
- self-test は人の道(実際の viewport pick、見えているボタン)。hidden widget 直叩き・UUID 注入だけを合格にしない。
- 「まとまり」は「グループ」と呼ぶ。Origin は Explorer の最上段。
- 押し出しの距離と板厚は別パラメータ(20mm 上限を漏らさない)。
- Enter/Esc は viewport の focus に依存しない。
- 進捗・完了の主張はそのセッションの tool 結果に紐づける。PC 未確認は PC_VERIFIED と書かない。
- MainWindow.cpp を太らせない(関数 100 行・ファイル 1500 行の門)。独立ファイルへ置く。
- PC への .cmd は Shift-JIS(cp932)+ CRLF で送る(UTF-8 だと日本語コメントが命令として実行される)。
- 本番で QDialog::exec() を据え付けると自己試験(offscreen)は永遠に止まる。窓は差し替え口(SetXxxChooser)にだけ入れ、本番は棚へ。
- 木(QTreeWidget)を作り直す関数(AdoptCurrentDocument/RefreshEntityList)を呼んだ後に QTreeWidgetItem* を触らない。必要な値は先に写す。
- 380px の棚に収めるには QFormLayout::WrapLongRows + AllNonFixedFieldsGrow、横並びは 3 つまで、長い説明はツールチップへ。
