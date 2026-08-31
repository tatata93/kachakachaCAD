# Linux / クラウド環境での検証ビルド

Windows の `scripts\check.ps1` に相当する検証を Linux(CIやAIエージェントのクラウド環境を含む)で行う手順。2026-08-31 に実際にこの手順で全16テストと `--self-test` の通過を確認済み。

## 必要な依存

- CMake 3.20+ / Ninja / GCC 13+(C++20)
- Qt 6.5 以上(qtbase のみで足りる。Widgets/Gui/Core を使用)
- Open CASCADE **master(8.1系以降)**。本プロジェクトは `occ::handle` や `GC_MakeSegment2d.hxx` など OCCT 7.9 には存在しない新APIを使っているため、リリース版 7.8/7.9 ではビルドできない。Windows側も OCCT のビルドツリー(`OpenCASCADE_DIR` 指定)を使う前提になっている(CMakeLists 参照)。

パッケージが取得できない閉域環境では、GitHub からソースを取得してビルドする:

```bash
# qtbase(最小構成、X11/OpenGL不要。offscreen プラットフォームで動かす)
git clone --depth 1 --branch v6.5.3 https://github.com/qt/qtbase.git
cmake -GNinja -S qtbase -B qtbase-build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/opt/qt6 \
  -DFEATURE_sql=OFF -DFEATURE_dbus=OFF -DFEATURE_network=OFF \
  -DINPUT_opengl=no -DFEATURE_vulkan=OFF -DFEATURE_icu=OFF \
  -DFEATURE_glib=OFF -DFEATURE_fontconfig=OFF -DFEATURE_xcb=OFF \
  -DFEATURE_system_zlib=OFF -DFEATURE_system_png=OFF \
  -DFEATURE_system_freetype=OFF -DFEATURE_system_harfbuzz=OFF \
  -DFEATURE_system_pcre2=OFF -DFEATURE_system_jpeg=OFF \
  -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF
ninja -C qtbase-build -j$(nproc) && ninja -C qtbase-build install

# OCCT(master。Visualization等は不要)
git clone --depth 1 https://github.com/Open-Cascade-SAS/OCCT.git
cmake -GNinja -S OCCT -B occt-build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/opt/occt \
  -DBUILD_MODULE_ApplicationFramework=OFF -DBUILD_MODULE_DataExchange=ON \
  -DBUILD_MODULE_DETools=OFF -DBUILD_MODULE_Draw=OFF \
  -DBUILD_MODULE_Visualization=OFF -DBUILD_DOC_Overview=OFF \
  -DUSE_FREETYPE=OFF -DUSE_TK=OFF -DUSE_TCL=OFF -DUSE_OPENGL=OFF \
  -DUSE_XLIB=OFF -DUSE_FREEIMAGE=OFF -DUSE_RAPIDJSON=OFF \
  -DUSE_DRACO=OFF -DUSE_TBB=OFF -DUSE_VTK=OFF
ninja -C occt-build -j$(nproc) && ninja -C occt-build install
```

注意: DataExchange 有効時は TKV3d などが `-lGL -lEGL` をリンクしようとする。GPU の無い環境ではダミーの空共有ライブラリを `libGL.so` / `libEGL.so` として配置すれば通る(本体はこれらのツールキットを使用しない)。実行時は `/opt/occt/lib` と `/opt/qt6/lib` を `ld.so.conf.d` か `LD_LIBRARY_PATH` に追加する。

## 本体のビルドと検証

```bash
cmake -S . -B build-full -GNinja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="/opt/qt6;/opt/occt"
cmake --build build-full            # 警告ゼロを維持すること
ctest --test-dir build-full         # 16テスト(qt_cad_smoke = --self-test を含む)
QT_QPA_PLATFORM=offscreen QT_QPA_FONTDIR=/usr/share/fonts/truetype/dejavu \
  ./build-full/kachakacha_cad --self-test
```

## GCC移植性の注意(MSVCでは通るがGCCで落ちるもの)

- `occ::handle<派生型>` から `occ::handle<基底型>` への**コピー初期化**(`handle<Base> b = derivedHandle;`)はGCCでコンパイルできない。**直接初期化**(`handle<Base> b(derivedHandle);`)か参照束縛を使うこと。
- 集成体初期化はフィールドを省略せず `{}` まで明示する(`-Wmissing-field-initializers`)。
- `src/apps/viewer`(Win32 GDI)は Linux ではビルド対象外。Qt本体アプリとテストが検証対象。
