"""Deliver reviewed photo revision and explicit comparison, without rehosting photos."""
from pathlib import Path
import argparse, html, json, shutil, sys
import assemble_revised


def comparison_page(data):
    esc=lambda value:html.escape(str(value),quote=True)
    rows=''.join('<tr>'+''.join('<td>'+esc(row[key])+'</td>' for key in
        ['part','before','real','change','limit'])+'</tr>' for row in data['items'])
    links=''.join('<li><a href="'+esc(row['url'])+'">'+esc(row['title'])+'</a></li>'
                  for row in data['referenceLinks'])
    concept=''.join('<li>'+esc(text)+'</li>' for text in data['conceptComparison']['observedDifferences'])
    return '''<!doctype html><html lang="ja"><meta charset="utf-8"><title>115系 部位別比較・写真照合改訂</title>
<style>body{max-width:1450px;margin:30px auto;padding:0 24px;font:16px/1.7 sans-serif;color:#26333c}img{max-width:100%;height:auto}section{margin:36px 0}table{border-collapse:collapse}th,td{border:1px solid #9aa8b1;padding:10px;vertical-align:top}th{background:#e5edf2}.pair{display:grid;grid-template-columns:1fr 1fr;gap:20px}.pair figure{margin:0}h1,h2{line-height:1.4}@media(max-width:800px){.pair{display:block}}</style>
<h1>115系の形を、部位ごとに比較する</h1><p>'''+esc(data['target'])+'''</p>
<p>'''+esc(data['basis'])+'''</p><p><a href="index.html">完成モデル・CAD・検証記録へ</a> / <a href="comparison.json">比較記録JSON</a></p>
<section><h2>前回のCAD形状と今回の改訂</h2><div class="pair"><figure><img src="before-front.png"><figcaption>前回配布モデルの実形状レンダリング</figcaption></figure><figure><img src="front-detail.png"><figcaption>今回の実CAD形状。生成予想画像ではありません。</figcaption></figure></div>
<p>'''+esc(data.get('summary','前面ガラス・灯具・屋根機器・スカート・台車を写真照合して改訂。'))+'''</p></section>
<section><h2>台車と屋根</h2><div class="pair"><figure><img src="before-bogie.png"><figcaption>前回の台車</figcaption></figure><figure><img src="bogie.png"><figcaption>曲線側枠・ばね・軸箱・局所押えを再確認。走行試験とは区別。</figcaption></figure></div><img src="top.png"><p>'''+esc(data.get('roofSummary','アンテナの架台・歩み板・ファン支持桟と手掛けを確認。'))+'''</p></section>
<section><h2>各部位の差と今回の扱い</h2><table><tr><th>部位</th><th>前回・途中状態</th><th>実車で確認した特徴</th><th>今回の変更</th><th>残る推定・簡略化</th></tr>'''+rows+'''</table></section>
<section><h2>以前の完成予想画像とも比較</h2><img src="previous-concept.png"><p>'''+esc(data['conceptComparison']['role'])+'''</p><ul>'''+concept+'''</ul></section>
<section><h2>実車写真・公式資料</h2><ul>'''+links+'''</ul><p>写真原本は作業中に実見。第三者写真はこの配布物へ転載せず、原ページを案内します。透視写真から局所寸法を測り切ったとは扱いません。</p></section></html>'''


def main():
    parser=argparse.ArgumentParser();parser.add_argument('workspace',type=Path)
    parser.add_argument('samples',type=Path);parser.add_argument('package',type=Path)
    parser.add_argument('--run',default='10');parser.add_argument('--comparison',required=True,type=Path)
    args=parser.parse_args();repo=Path(__file__).resolve().parents[3]
    with_argv=[sys.argv[0],str(args.workspace),str(args.samples),str(args.package),'--run',args.run]
    old=sys.argv
    try:sys.argv=with_argv;assemble_revised.main()
    finally:sys.argv=old
    proof=json.loads((args.workspace/'review-result.json').read_text(encoding='utf-8'))
    design=json.loads((args.workspace/('candidate'+args.run)/'design.json').read_text(encoding='utf-8'))
    assert proof['passed'] and design.get('detailRevision')
    data=json.loads((args.comparison/'comparison.json').read_text(encoding='utf-8'))
    assert len(data['items'])==10 and (args.comparison/'previous-concept.png').is_file()
    page=comparison_page(data)
    for destination in [args.samples,args.package]:
        shutil.copy2(args.comparison/'comparison.json',destination/'comparison.json')
        shutil.copy2(args.comparison/'previous-concept.png',destination/'previous-concept.png')
        for before,after in [('front-detail.png','before-front.png'),('bogie.png','before-bogie.png')]:
            shutil.copy2(repo/data['previousModel']/before,destination/after)
        shutil.copy2(repo/'samples/series115-ho-new-20261024/front-detail.png',destination/'audit-initial-front.png')
        audit=destination/'prototype-audit.html'
        audit.write_text(audit.read_text(encoding='utf-8').replace(
            '../../samples/series115-ho-new-20261024/front-detail.png','audit-initial-front.png').replace(
            'kuha115-prototype-audit-20261024.json','prototype-audit.json').replace(
            '../../samples/series115-ho-new-20261024/index.html','audit-initial-front.png'),encoding='utf-8',newline='\n')
        (destination/'comparison.html').write_text(page,encoding='utf-8',newline='\n')
        readme=(destination/'README.md').read_text(encoding='utf-8')
        readme=readme.replace('車番レタリングは収録していない。','普通幕・クハ115-1222標記は印刷用画像として同梱し、ソリッドにはしない。')
        readme=readme.replace('通常の弦誤差0.025 mm、通風器のみ0.006 mm。','標準STL弦誤差0.025 mm、屋根・通風器・表示箱は0.006 mm、薄い前照灯反射鏡/レンズは0.001 mm。')
        readme=readme.replace('各版227点',f"各版{proof['models'][0]['pointChecks']}点")
        readme=readme.replace('アプリ本体は変更しておらず、製品全体試験の再実行をしたという意味ではない。',
            'CAD本体の検証と模型の検証は別に記録する。使用したCADの実行ファイルSHAはvalidation-summary.jsonで照合する。')
        readme=readme.replace('第三者の写真・PDF、AI完成予想図はこの配布モデルの実形状画像に置き換えており、同梱していない。',
            '第三者写真・PDFは転載しない。以前のAI完成予想図はcomparison.htmlで実CAD形状と明確に区別して同梱する。')
        if design['detailRevision']=='2026-10-25-shoulder-refinement':
            readme=readme.replace('屋根断面厚0.85 mm','屋根の目標肉厚1.0 mm（肩を含む0.85 mm法線サンプル検査付き。全面の最小肉厚保証とは区別）')
            readme=readme.replace('床板厚1.3 mm','床板の基板厚1.3 mm（前端台枠は床板と一体）')
        readme+=f"\n初回の履歴再構築は複雑なBooleanを含むため重い。幾何検査に使ったCADでは、独立キャッシュでのWindows表示確認に{proof['desktopStartup']['seconds']:.1f}秒を要した（他の模型検査と並行した単発確認）。この版の計算済みデータは約136 MBで、当時の128 MiB上限ではキャッシュされなかった。キャッシュ上限修正の最新結果はリポジトリのdocs/development/image-io-performance-20261025.htmlを参照。初回を即時読込できるモデルとは扱わない。\n"
        readme+='\n## 今回の写真照合改訂\n\n[部位別の実車・前回CAD・完成予想画像との比較](comparison.html)を参照。\n'
        (destination/'README.md').write_text(readme,encoding='utf-8',newline='\n')
        index=(destination/'index.html').read_text(encoding='utf-8')
        index=index.replace('<nav>','<nav><a href="comparison.html">部位別の比較と差異</a>')
        index=index.replace('kuha115-ho80-revised-20261024.zip',args.package.name+'.zip')
        index=index.replace('貫通扉窓を横長に修正。',html.escape(data.get('summary','前頭部を写真照合して改訂。'))+' 貫通扉窓は横長を維持。')
        (destination/'index.html').write_text(index,encoding='utf-8',newline='\n')


if __name__=='__main__':main()
