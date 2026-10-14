"""Generate portable presentation material from measured verification evidence."""
import argparse
import csv
import html
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
DEST = ROOT / 'docs/development'


def flow_svg():
    labels = [
        ['依頼・広い調査', '機能修正・大きな再ビルド', '全体検証を開始', '遅い段階で整合不良・実モデル不具合を発見', '修正して全体検証を再実行', '固定の配布先を更新', 'コミット中心の証拠・push'],
        ['依頼・実モデル再現・範囲決定', '実装＋必要な読み取り専用調査', '短い整合検査・依存を含む重点検証', '完成候補を固定・ソースと環境を記録', '全件＋実際の配布版を検証', '証拠と生成物の一致を確認', '検証済み版を別フォルダーへ配布・push']]
    parts = ['<svg xmlns="http://www.w3.org/2000/svg" width="1400" height="830" viewBox="0 0 1400 830"><rect width="1400" height="830" fill="white"/><g font-family="sans-serif" fill="#202c39"><text x="50" y="45" font-size="28">従来の工程</text><text x="750" y="45" font-size="28">変更後の工程</text>']
    for column, rows in enumerate(labels):
        x = 45 + column * 700
        color = '#fae8e8' if column == 0 else '#e6f2ff'
        for i, text in enumerate(rows):
            y = 80 + i * 94
            parts.append(f'<rect x="{x}" y="{y}" width="610" height="64" rx="8" fill="{color}" stroke="#8294a6"/><text x="{x+18}" y="{y+39}" font-size="21">{html.escape(text)}</text>')
            if i < len(rows) - 1:
                parts.append(f'<path d="M{x+305} {y+67}v23m-7-8 7 8 7-8" fill="none" stroke="#566a7d" stroke-width="3"/>')
    parts.append('<text x="50" y="785" font-size="20">問題：検査が後ろ、固定出力、再実行が長い</text><text x="750" y="785" font-size="20">全件試験・配布版試験・精度は維持</text></g></svg>')
    return '\n'.join(parts)


def dependency_svg():
    return '''<svg xmlns="http://www.w3.org/2000/svg" width="1400" height="500" viewBox="0 0 1400 500">
<rect width="1400" height="500" fill="white"/><g font-family="sans-serif" font-size="22" fill="#202c39">
<text x="40" y="40" font-size="28">厚み状態の依存構造：代表範囲の改善</text>
<rect x="40" y="85" width="600" height="110" rx="8" fill="#fae8e8"/><text x="60" y="125">共通窓ヘッダー → 共通の状態定義</text><text x="60" y="163">厚み入力型・Dock・カーネルを間接公開</text>
<path d="M340 205v55m-8-10 8 10 8-10" fill="none" stroke="#566a7d" stroke-width="3"/>
<text x="60" y="300">共有入力ヘッダー変更：157ファイル再コンパイル</text>
<rect x="730" y="85" width="625" height="110" rx="8" fill="#e6f2ff"/><text x="750" y="125">窓ヘッダーは状態を前方宣言し、単独所有</text><text x="750" y="163">専用状態ヘッダーを所有する4 cppへ限定</text>
<path d="M1040 205v55m-8-10 8 10 8-10" fill="none" stroke="#566a7d" stroke-width="3"/>
<text x="750" y="300">同じ共有入力ヘッダー変更：14ファイル</text>
<text x="40" y="390">残る制約：V2MainWindow.h自体を変更すると、引き続き155 TUに波及する。</text>
<text x="40" y="440">型の状態・初期値・解放と移譲・公開操作APIを維持。削除した機能やソースファイルはない。</text>
</g></svg>'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--evidence', type=Path)
    args = parser.parse_args()
    DEST.mkdir(parents=True, exist_ok=True)
    metrics = json.loads((DEST / 'build-measurements.json').read_text(encoding='utf-8'))
    parallel = json.loads((ROOT / 'out/verification/parallel-comparison/results.json').read_text())
    public_parallel = json.loads(json.dumps(parallel).replace(ROOT.as_posix(), '${repo}').replace(str(ROOT).replace('\\', '\\\\'), '${repo}'))
    (DEST / 'parallel-measurements.json').write_text(json.dumps(public_parallel, indent=2), encoding='utf-8')
    (DEST / 'workflow-before-after.svg').write_text(flow_svg(), encoding='utf-8')
    (DEST / 'dependency-before-after.svg').write_text(dependency_svg(), encoding='utf-8')
    with (DEST / 'build-measurements.csv').open('w', encoding='utf-8-sig', newline='') as out:
        writer = csv.DictWriter(out, fieldnames=list(metrics['measurements'][0]))
        writer.writeheader(); writer.writerows(metrics['measurements'])
    rows = ''.join(f'<tr><td>{html.escape(r["condition"])}</td><td>{r["before_seconds"]:.2f}秒</td><td>{r["after_seconds"]:.2f}秒</td><td>{r["before_compiled"]} → {r["after_compiled"]}</td></tr>' for r in metrics['measurements'])
    proof = json.loads(args.evidence.read_text(encoding='utf-8')) if args.evidence else None
    verification = '全体検証は実施前。測定と重点検証の結果のみ。'
    if proof:
        verification = f'全体状態: {proof["status"]}。CTest {proof.get("ctest", {}).get("total", 0)}件、UI {proof.get("ui_cases", [])}件。開始 {proof["start"]}、終了 {proof["end"]}。検証ソース {proof["source"]["commit"]}。'
    body = f'''<!doctype html><html lang="ja"><meta charset="utf-8"><title>kachakachaCAD 開発待ち時間の改善</title>
<style>body{{font:16px/1.7 system-ui,sans-serif;color:#202c39;max-width:1180px;margin:32px auto;padding:0 24px}}h1{{font-size:30px}}h2{{font-size:23px;margin-top:38px}}table{{width:100%;border-collapse:collapse}}td,th{{padding:9px;border:1px solid #ccd6df;text-align:left}}th{{background:#eef3f7}}img{{width:100%}}pre{{background:#eef3f7;padding:16px;white-space:pre-wrap;overflow-wrap:anywhere}}.note{{background:#fff3d9;padding:16px}}@media print{{body{{font-size:11pt;margin:0;max-width:none}}h2{{break-after:avoid}}img,table{{break-inside:avoid}}}}</style>
<h1>kachakachaCAD 開発待ち時間の改善</h1><p>2026-10-14 ／ 発表資料へ転用するための工程・構成・実測比較。図は別添SVG、数値はCSV/JSONでも提供。</p>
<div class="note">今回の基盤整備と、将来の小修正の待ち時間を区別する。共有入力ヘッダーの代表測定は約89.3%短縮。全修正で30分達成したという結果ではない。</div>
<h2>1. 背景と問題</h2><p>過去5回の全体ゲート14回中8回が失敗し再実行。最新の過去CTestは約18分30秒、開発版GUI約7分44秒、配布生成・配布版試験約8分7秒。ツール外経過は約52%だが、内部思考だけの時間とは断定できない。これらは過去の参考値で、今回の同条件速度比較ではない。</p>
<h2>2. 作業工程の比較</h2><img src="workflow-before-after.svg" alt="従来と変更後の工程"/>
<table><tr><th>箇所</th><th>従来の問題</th><th>変更後の対応・残る制約</th></tr>
<tr><td>検証入口</td><td>ローカルの未管理スクリプト、固定ブランチと変更許可リスト</td><td>Git管理された入口、段階と証拠を分離。未知・共有変更は全体へ拡張</td></tr>
<tr><td>検証順序</td><td>整合不良や実モデル不具合の発見が遅い。check-v2はV2試験を重複実行</td><td>実モデル再現と短い9試験を先行。完成候補の全体ゲートは維持</td></tr>
<tr><td>対象漏れ</td><td>重点フィルターが全件・配布試験へ残り得る</td><td>正確な一覧照合、明示的全件指定、0件・未知・重複・失敗・skip・古い証拠を拒否</td></tr>
<tr><td>並列処理</td><td>固定TEMP、共有キャッシュ、配布先、ビルドexeが衝突</td><td>実行・試験ごとの分離、ビルドツリーの排他、GUI/配布資源を管理。配布物から起動</td></tr>
<tr><td>配布検査</td><td>開発環境のDLLやプラグインで不足が隠れる可能性</td><td>配布版の全件試験を保持し、qt.confと検索環境を分離</td></tr>
<tr><td>担当運用</td><td>大きな文脈や重複調査、実行資源の競合</td><td>主担当＋必要時の読み取り専用2担当。実行は主担当が管理</td></tr></table>
<h2>3. 依存構造と再ビルド</h2><img src="dependency-before-after.svg" alt="依存構造の比較"/>
<table><tr><th>代表条件</th><th>変更前</th><th>変更後</th><th>再コンパイル数</th></tr>{rows}</table>
<p>同じWindows PC、MSVC Release、アプリターゲット、並列2。ウォームアップを除く増分ビルドを各条件1回測定。mtimeのみを変更し、ソースのバイト列は維持。共有入力ヘッダーは同じThickenInputState.h。新状態ヘッダーの直接所有は4 cppだが、この測定では他の直接使用箇所も含め14ファイルとなる。小さい差の再現性や平均値は未評価。</p>
<p>自己試験用ターゲットの全面分離は今回見送り。単に静的ライブラリへまとめてもコンパイル削減にならず、別runner化には既存--self-testと配布契約の整理が必要。代表状態の分離で効果が得られたため、全面移行を混ぜない。</p>
<h2>4. 並列検証の代表測定</h2><p>同じ17試験を逐次→並列2の順に実行。{parallel[0]['steps'][-1]['seconds']:.2f}秒 → {parallel[1]['steps'][-1]['seconds']:.2f}秒。試験名・件数・成功条件は同じ。全体ゲートの削減率ではなく、この代表群の結果。初回/後続のファイルキャッシュ等の影響は残る。</p>
<h2>5. フォルダー・ファイル構成</h2><table><tr><th>従来</th><th>変更後</th></tr><tr><td><pre>_GO.cmd / _QUICK.cmd（ローカル）
  └ _claudeout/gpt-current.py（未管理）
      ├ gpt-current-check.txt（同名を更新）
      └ gpt-verified.json（コミット中心）
scripts/check*.ps1 / test.ps1
out/kachakachaCAD-v2（再生成時に置換）
V2MainWindowTypes.h（厚み状態も公開）</pre></td><td><pre>scripts/verify.py / verify.ps1 / verify.sh
scripts/_GO.cmd / _QUICK.cmd（正式入口）
scripts/verification/
  runner.py / selection.py / scopes.json
  evidence.py / isolated_test.py / tests.py
  benchmark.py / make_comparison.py
scripts/run-verified.py
out/verification/&lt;実行ID&gt;/
  result.json / *.log / *.xml
  selftest-*.json / isolated/ / package/
out/releases/&lt;実行ID&gt;/
  package/ / package.zip / verification.json
V2ThickenPreviewState.h（非公開・単独所有）
docs/development/（手順と比較資料）</pre></td></tr></table>
<h2>6. 不要・廃止・削除・保全の区別</h2><table><tr><th>対象</th><th>扱いと理由</th></tr>
<tr><td>_claudeout/gpt-current.py</td><td>正式な通常検証からの依存を廃止。歴史的Git公開フラグの互換処理用にローカルでは保全。新手順はこれなしで再現できる</td></tr>
<tr><td>旧gpt-current-check.txt / gpt-verified.json</td><td>新手順では使用しない。過去の証拠として保全</td></tr>
<tr><td>ルートのローカル_GO.cmd / _QUICK.cmd</td><td>通常経路はGit管理された入口へ委譲。旧内容はout/verification/legacy-entry-backupへ保全。Git公開の旧フラグは警告して旧経路へ委譲</td></tr>
<tr><td>V2MainWindowTypes.h等の旧ヘッダー</td><td>他の型に必要なので存続。削除したのは厚み状態の旧定義と不要なinclude辺。状態定義は専用ファイルへ移動</td></tr>
<tr><td>古いworktree・配布版・バックアップ・増分ビルド</td><td>削除しない。所有者と用途を保全</td></tr>
<tr><td>実際に削除したソースファイル</td><td>なし。容量を理由とする削除や機能・試験の削除もなし</td></tr></table>
<h2>7. 検証結果と限界</h2><p>{html.escape(verification)}</p><p>基盤の負の経路を7単体試験で検査。実アプリでも空・未知・重複選択を拒否。整合チェック9件が成功。ユーザー実モデルの厚み操作・値変更・再利用が成功し、原本SHA256不変。これらを全体検証と区別する。</p>
<p>全体GUI試験2回は残る。主ウィンドウの共通ヘッダーの波及も残る。代表的小修正の依頼開始から検証済み配布版までの30分達成は未評価で、次の通常開発で測る。今回の整備時間やコメントだけの重点検証試行を、実際の機能修正の待ち時間と呼ばない。</p>
<h2>8. 今後のコマンド</h2><pre>python scripts/verify.py plan
python scripts/verify.py preflight
python scripts/verify.py focused
python scripts/verify.py full
python scripts/verify.py publish --evidence out/verification/&lt;実行ID&gt;/result.json
python scripts/run-verified.py out/releases/&lt;実行ID&gt;
./scripts/verify.sh focused linux-core</pre><p>初回・未知変更・検証環境の変更では全体へ拡張。重点検証は途中確認であり配布証拠にはならない。コミット済み入力の全体検証後にGit pushする場合はfull --push。図のSVGと数値CSVは発表資料へそのまま転用できる。</p>
<h2>添付データ</h2><p><a href="build-measurements.csv">ビルド比較CSV</a> ／ <a href="build-measurements.json">ビルド比較JSON</a> ／ <a href="parallel-measurements.json">並列比較JSON</a> ／ <a href="verification-workflow.md">正式な手順</a></p></html>'''
    (DEST / 'latency-comparison.html').write_text(body, encoding='utf-8')
    print(DEST / 'latency-comparison.html')


if __name__ == '__main__':
    main()
