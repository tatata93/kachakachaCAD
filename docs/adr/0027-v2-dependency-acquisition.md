# ADR 0027: Wire-first V2 の依存取得方法

- 日付: 2026-09-07
- 状態: 承認
- 関係: `docs/v2/implementation-work-packages.md` WP-01、`docs/v2/pre-implementation-fixes.md` B-4

## 背景

`docs/v2/implementation-work-packages.md` の WP-01 は
「`nlohmann-json` と `libzip` を manifest へ固定する」と定めている。
`.kcd2` は ZIP + JSON なので、この2つは WP-05 で必要になる。

しかし現状のビルドは **vcpkg のクラシックモード**で動いている。

- `cmake/KachakachaEnvironment.cmake` の `kachakacha_detect_vcpkg()` が
  vcpkg を自動検出し、`CMAKE_TOOLCHAIN_FILE` を設定する。
- リポジトリに `vcpkg.json` は存在しない。
- Qt 6.9.2 と OCCT 8.0.1 は vcpkg のマニフェスト経由ではなく、
  `find_package` が探す既存のインストールから解決している。

ここへ `vcpkg.json` を置くと、vcpkg は**マニフェストモードへ自動的に切り替わる**。
マニフェストに Qt と OCCT が入っていなければ `find_package` が失敗し、
入れれば Qt と OCCT をソースからビルドし始める。
どちらも、ビルドできる唯一のPCを止める。

## 決定

**初回実装では `vcpkg.json` を追加しない。**
`nlohmann/json` と ZIP 読み書きは、WP-05 の着手時に次の順で検討する。

1. **単一ヘッダ/単一ソースの同梱を第一候補とする。**
   `nlohmann/json`(MIT、単一ヘッダ)と `miniz`(MIT、.c/.h 各1本)は
   `third_party/` へ置けば、ビルド時のネットワークも vcpkg の状態も要らない。
   `docs/licensing-audit.md` へ追記する。
2. 同梱が不都合な事情が出た場合にだけ、マニフェストモードへの移行を
   別ADRとして起こす。その際は Qt と OCCT の解決方法を先に決め、
   Windows実機で1度通してから他の作業へ渡す。

WP-01 の完了条件から「`vcpkg.json` を追加する」を外し、
「依存取得方法をADRで決め、ビルドを壊さない」に読み替える。

## 理由

- 依存の取り方は製品契約ではなく実装詳細であり、統合担当が決めてよい範囲である
  (`docs/v2/integration-lead-prompt.md` §9)。
- V2の期間中、Windows実機は1台しかない。そこが止まると全WPが止まる。
- 単一ヘッダの同梱は、この2つのライブラリでは一般的な使い方であり、
  ライセンス上も問題がない(いずれもMIT)。

## 影響

- WP-01 は `vcpkg.json` を作らない。`docs/v2/implementation-work-packages.md` の
  WP-01 所有物からも外す。
- WP-05 着手時に `third_party/` を追加し、`docs/licensing-audit.md` を更新する。
- 既存のビルド経路は一切変えない。
