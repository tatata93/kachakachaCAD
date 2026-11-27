# 学校PCの初回設定記録

2026-11-27、GitHub Issue #1。学校PCの検証用worktreeは
`be44649090add1a302dfacece350817a88e7c629` を基点とする。運用文書は
`codex/home-school-development` の `ee83a244f` を参照した。

- 元の `main` は `d3893fa6`。開発基点より887コミット前。
  元の作業ツリーと所有者未確認のファイルは別の場所に残した。
- Windows x64、Git 2.46.1、CMake 4.3.3、Python 3.14.3、VS 2022の
  MSVC 19.44.35228.0、Qt 6.9.2（msvc2022_64）を確認した。
- CIと同じOCCTのソース `3d097a0328e71b826377d4814ab05ec3c3d23871`
  を使った。初回はMSVCが生成ヘッダー中のUTF-8の日本語パスを
  既定のコードページで読んでC1083を出した。依存のCMakeキャッシュで
  `CMAKE_CXX_FLAGS=/utf-8 /MP8` を指定したところ、TKernel.dllを生成し、
  以前のエラー箇所を通過した。
- 公開版 `v0.1.0-20261127-update1` で、無題から線ツールを選び、
  同じ作業平面の2点をクリックして直線を作った。3D画面で線を選択して
  1件の強調表示を確認し、Ctrl+Zで消え、Ctrl+Yで戻ることを確認した。
  Ctrl+Sで試験文書を保存し、Ctrl+Nで空の文書に切り替え、Ctrl+Oで
  保存文書を開いて直線が表示されることを確認した。
- 試験文書 `s.kcd2` のSHA-256は
  `ed2f1e2ea125ddc2ddcd889ae7ccb2447a98d31e6b50de01eca92a85337cf0d5`。
  スクリーンショットと文書は学校PCの `out/school-initial-setup` に保存した。

この記録時点ではOCCT全体のビルド・インストール、開発版のQt/OCCT構成、
開発版の起動・自己試験、WP-08の製品修正は検証待ち。
