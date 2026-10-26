"""Add an honest, measured attachment review to the newly generated delivery."""
from pathlib import Path
import argparse,hashlib,html,json,shutil


def main():
    ap=argparse.ArgumentParser();ap.add_argument('workspace',type=Path);ap.add_argument('samples',type=Path);ap.add_argument('--run',default='03');a=ap.parse_args()
    w,s,r=a.workspace,a.samples,a.run;read=lambda p:json.loads(p.read_text(encoding='utf-8'))
    proof=read(w/'review-result.json');negative=read(w/'negative04/results.json');assert proof['passed'] and negative['passed']
    design=read(w/('candidate'+r)/'design.json');report=read(w/('exports'+r)/'print-parts.json')
    assert negative['designSha256']==hashlib.sha256((w/('candidate'+r)/'design.json').read_bytes()).hexdigest()
    assert len(report['attachmentChecks'])==len(design['attachmentChecks'])>0 and all(c['passed'] for c in report['attachmentChecks'])
    for name in ['cooler-seats','skirt-arms']:shutil.copy2(w/('attachments'+r)/(name+'.png'),s/(name+'.png'))
    shutil.copy2(w/'negative04/results.json',s/'attachment-negative-tests.json')
    count=len(report['attachmentChecks']);full,split=proof['models']
    evidence=dict(revision=design['attachmentRevision'],checks=report['attachmentChecks'],models=proof['models'],
        negative=negative,limits=['Model construction dimensions are estimates, not measured prototype fittings.',
            'No physical printing, adhesive strength or running trial.'],removedFiles=[])
    repo=Path(__file__).resolve().parents[3]
    evidence['sourceSha256']={str(p.relative_to(repo)).replace('\\','/'):hashlib.sha256(p.read_bytes()).hexdigest()
        for p in [Path(__file__).resolve(),repo/'tools/modeling/kuha115_ho/fit_details.py',
            repo/'tools/modeling/kuha115_ho/revised.py',repo/'tools/modeling/kuha115_ho/render_fit.py',repo/'tools/modeling/kuha115_ho/test_attachment_fit.py']}
    (s/'attachment-review.json').write_text(json.dumps(evidence,ensure_ascii=False,indent=2),encoding='utf-8')
    rows=''.join('<tr><td>'+html.escape(c['label'])+'</td><td>'+f"{c['distanceMm']:.6g}"+'</td><td>'+f"{c['contactAreaMm2']:.4f}"+'</td></tr>' for c in report['attachmentChecks'] if 'サドル' in c['label'] or 'スカート' in c['label'])
    page=f'''<!doctype html><html lang="ja"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>クハ115 1/80 取付修正版</title>
<style>body{{max-width:1200px;margin:30px auto;padding:0 22px;font:17px/1.8 sans-serif;color:#25333b}}img{{max-width:100%;height:auto}}section{{margin:36px 0}}table{{border-collapse:collapse}}td,th{{padding:8px;border:1px solid #aab5bb}}.pair{{display:grid;grid-template-columns:1fr 1fr;gap:16px}}figure{{margin:12px 0}}@media(max-width:700px){{.pair{{display:block}}}}</style>
<h1>クハ115-1000 長野色・1/80 HO16.5<br>ガラス・スカート・クーラーの取り付けを修正</h1>
<p>前版の外観を保ち、離れていた取付面と接着代を修正しました。通常版{full['parts']}部品／前後分割版{split['parts']}部品。局所寸法は写真に基づく模型設計値で、実車の完全な実測復元ではありません。</p>
<p><a href="kuha115-ho80-revised-saved.kcd2">通常版CADを開く</a> ／ <a href="kuha115-ho80-revised-split-saved.kcd2">前後分割版CAD</a> ／ <a href="README.md">組立と制限</a> ／ <a href="attachment-review.json">取付の検査記録</a></p>
<img src="overview.png" alt="改訂したクハ115の実CAD形状">
<section><h2>屋根に沿うクーラーの支持座</h2><p>以前にも奥まった4本の脚はありましたが、側面から浮いて見えました。2本の横断サドルをキセへ一体化し、実際の屋根曲面で底を切っています。前後・左右の4領域で接触面積を検査しました。</p><img src="cooler-seats.png" alt="屋根曲面で底を切ったクーラー支持座"><p>形を見分けるため、屋根を青くした実CAD形状の部分表示です。実車写真やAI予想図ではありません。</p></section>
<section><h2>床板からスカートへつながる支持腕</h2><p>床板下面から左右の立上りと水平腕を一体で伸ばしました。スカート裏面には各3.9 mm²の接着面が接します。中央の連結器位置を避けています。</p><img src="skirt-arms.png" alt="床板の支持腕とスカートの接触"><p>青が床板と一体の支持腕、茶がスカート。前端だけを切り出した表示です。</p></section>
<section><h2>ガラスを内側へ納め、接着受けを設ける</h2><p>側窓・客扉・乗務員扉・貫通扉・後妻扉のガラスを相手の裏面に合わせました。パノラミックガラスは外面を車体より0.22 mm内側に保ち、裏側の曲面受けで支えます。窓の視界を塞がず、Hゴムも対応面へ接触させています。</p><div class="pair"><figure><img src="before-front.png"><figcaption>前版の実形状</figcaption></figure><figure><img src="front-detail.png"><figcaption>取付修正版の実形状</figcaption></figure></div></section>
<section><h2>実形状による検査</h2><p>各版で{count}か所の接触を検査しました。距離だけでなく、元の境界面同士に十分な共通面積があることを確認しています。領域で切った人工面を接触面積には数えません。</p>
<table><tr><th>取付箇所</th><th>距離 mm</th><th>接触面積 mm²</th></tr>{rows}</table>
<p>スカートを0.5 mm、ガラスを0.2 mm、屋根を0.5 mm離す3つの負の試験は、すべて不合格になりました。通常版・分割版の全閉ソリッド、有効BRep、正体積、全STEP再読込、全STL閉鎖性・面向き、各476点、全対候補の干渉、分割6組の再組立、保存・再読込も検査しました。</p>
<p><a href="validation-summary.json">全体検査</a> ／ <a href="review-negative-tests.json">欠落・古い証拠を拒否する検査</a> ／ <a href="attachment-negative-tests.json">部品を離す試験</a></p></section>
<section><h2>実際のWindows版での表示</h2><img src="cad-screen.png" alt="Windows版CADで改訂モデルを開いた画面"><p>通常のWindowsイベントループで表示を確認した画面です。全モデリング工程をOSのマウスで作り直した証拠ではありません。</p></section>
<p>実印刷、接着強度、走行、旋回する台車の動的干渉、市販動力との互換は未試験です。以前のモデル・ユーザーの原本・旧配布は削除していません。</p></html>'''
    (s/'index.html').write_text(page,encoding='utf-8',newline='\n')
    readme=s/'README.md';text=readme.read_text(encoding='utf-8')
    prefix='''# 2026-10-26 取付修正版

通常版は `kuha115-ho80-revised-saved.kcd2`、小型プリンタ向け前後分割版は `kuha115-ho80-revised-split-saved.kcd2` を開く。
スカートの支持腕は床板と一体。スカートを腕の前面へ接着する。クーラーの2本のサドルは屋根曲面へ合わせて接着する。
ガラスは各窓の内側の受け／扉裏面へ接着する。模型設計寸法であり、接着強度・実印刷・走行は未試験。
接触・すき間・面積・負の試験は `attachment-review.json` と `index.html` を参照。
旧モデル・元のユーザーファイルを上書き・削除していない。

'''
    if not text.startswith('# 2026-10-26 取付修正版'):text=prefix+text
    readme.write_text(text,encoding='utf-8',newline='\n');print('Attachment delivery:',s,count,'checks per model')


if __name__=='__main__':main()
