"""Deliver three corrected shapes, retaining before/after geometry and limits."""
from pathlib import Path
import argparse,hashlib,html,json,shutil,sys
import assemble_revised

PHOTO='https://kokuden-library.com/2025/12/22/jr%E6%9D%B1%E6%97%A5%E6%9C%AC115%E7%B3%BB1000%E7%95%AA%E5%8F%B0-%E9%95%B7%E3%83%8A%E3%83%8En15%E7%B7%A8%E6%88%90%EF%BC%88%E8%A8%93%E7%B7%B4%E8%BB%8A%EF%BC%89/'
MAKER='https://www.giho.mitsubishielectric.co.jp/giho/pdf/1982/8211.pdf'


def main():
    ap=argparse.ArgumentParser();ap.add_argument('workspace',type=Path);ap.add_argument('samples',type=Path)
    ap.add_argument('package',type=Path);ap.add_argument('--run',default='04');a=ap.parse_args();w=a.workspace
    read=lambda p:json.loads(p.read_text(encoding='utf-8'))
    proof=read(w/'review-result.json');shape=read(w/('negative-old-geometry'+a.run)/'results.json')
    design=read(w/('candidate'+a.run)/'design.json');assert proof['passed'] and shape['passed'] and design['contours']
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    assert shape['candidateDesignSha256']==sha(w/('candidate'+a.run)/'design.json')
    assert shape['candidateExportReportSha256']==sha(w/('exports'+a.run)/'print-parts.json')
    for entry,suffix in zip(proof['models'],['','-split']):
        assert entry['sourceSha256']==sha(w/('candidate'+a.run)/('kuha115-ho80-revised'+suffix+'.kcd2'))
    old=sys.argv
    try:
        sys.argv=[sys.argv[0],str(w),str(a.samples),str(a.package),'--run',a.run];assemble_revised.main()
    finally:sys.argv=old
    full,split=proof['models']
    report=read(w/('exports'+a.run)/'print-parts.json')
    cooler=next(p for p in report['parts'] if p['id']==design['keyParts']['climateUnit'][0])
    rows=''.join('<tr>'+''.join('<td>'+html.escape(str(m[k]))+'</td>' for k in ['name','parts','pointChecks','attachmentChecks','brepInterferenceCandidates','splitReassemblyChecks'])+'</tr>' for m in proof['models'])
    pair=lambda stem,label: '<div class="pair"><figure><img src="before-'+stem+'.png"><figcaption>前版</figcaption></figure><figure><img src="'+stem+'.png"><figcaption>'+label+'</figcaption></figure></div>'
    page=f'''<!doctype html><html lang="ja"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>115系・屋根への納まり、床板一体スカート、扉裾絞り</title>
<style>body{{max-width:1200px;margin:30px auto;padding:0 22px;font:17px/1.8 sans-serif;color:#24343f}}img{{max-width:100%;height:auto}}section{{margin:40px 0}}table{{border-collapse:collapse}}td,th{{border:1px solid #abb7bd;padding:8px}}.pair{{display:grid;grid-template-columns:1fr 1fr;gap:16px}}figure{{margin:12px 0}}@media(max-width:700px){{.pair{{display:block}}}}</style>
<h1>クハ115-1000 長野色・1/80 HO16.5<br>支持柱で埋める形を改め、部品の外形を修正</h1>
<p>前版は接触検査に偏り、クーラー本体の下の隙間や平板の扉を残していました。今回はクーラー下面、スカートから床への連続性、扉の断面を修正しました。局所寸法は模型設計値です。</p>
<p><a href="kuha115-ho80-revised-saved.kcd2">通常版CAD</a> ／ <a href="kuha115-ho80-revised-split-saved.kcd2">前後分割版CAD</a> ／ <a href="README.md">組立手順・制限</a> ／ <a href="validation-summary.json">検証記録</a></p>
<img src="overview.png" alt="今回生成した実CAD形状の全景">
<section><h2>クーラー：丸いキセの下縁を屋根へ納める</h2>
<p>高い2本のサドルを廃し、本体を低く配置して端板と長辺下縁を連続させ、下面を実際の屋根曲面で切りました。半円をそのまま置くのではなく、緩いアーチの上面と屋根に沿う下縁です。</p>
{pair('cooler-side','今回：大きな脚と下部の空隙を解消')}
<img src="cooler-end.png" alt="屋根に沿うキセの端板と幅方向アーチ"><p>青は納まり確認用の屋根色。実形状のSTL描画で、実車写真やCADの操作画面ではありません。メーカー公表外形は参考にしましたが、屋根との取付高さは写真からの推定です。平滑なキセ面の頂点Z48.15 mm、補強帯等を含む上端Z{cooler['bounds'][5]:.3f} mm、屋根頂点Z45.675 mm。</p></section>
<section><h2>スカート：床板から連続する一体部品にする</h2>
<p>細い水平腕と立上りを廃し、スカートの左右上縁から幅広い返しを床板へつなぎ、床板・返し・スカートを1つの閉ソリッドにしました。中央の連結器とジャンパ周辺は開放しています。</p>
{pair('floor-apron','今回：青い全体が床板とスカートの一体部品')}
<p>これは模型を一体印刷するための接続設計です。実車のスカートが床板の一枚板を延ばした構造だとは断定しません。前後分割版では前半床板とスカートが一体です。</p></section>
<section><h2>ドア：下部を車体の裾絞りに合わせる</h2>
<p>客扉12枚と乗務員扉2枚を修正。上部の窓位置を保ち、Z24 mm以下で下端へ向かって内側に絞ります。車体と同じ断面則を使い、表面を周囲外板から0.28 mm奥へ置きました。客扉下端の外面は中心から16.921 mmで、前版の17.845 mmより約0.924 mm内側です。</p>
<img src="door-sections.png" alt="旧平板と新しい裾絞り扉の実CAD断面比較">
<p>緑は隣接車体、茶は扉。窓より下の両端と複数高さを実BRepで検査しています。局所の曲げR・折れ位置は実車の実測復元ではありません。踏板も外板へ食い込まない裏面へ修正しました。</p>
{pair('door-detail','今回：下端が内側へ入る扉')}
</section>
<section><h2>今回の検証</h2><table><tr><th>版</th><th>部品</th><th>点</th><th>接触</th><th>全対の干渉候補</th><th>分割再組立</th></tr>{rows}</table>
<p>全可視部品が有効な1閉ソリッド・正体積。全STEP再読込、全STL閉鎖性・面向き・退化・体積差、保存再読込を確認しました。全対検査の許容値1e-6 mm³、STL体積差上限0.2%は緩和していません。</p>
<p>新しい断面検査を旧モデルにも適用し、クーラーの空隙・床板への非連続・14枚の平板扉の計16件をすべて不合格として検出しました。<a href="contour-negative-tests.json">旧形状を拒否する記録</a> ／ <a href="review-negative-tests.json">欠落・古い証拠を拒否する記録</a> ／ <a href="door-sections.json">断面線データ</a></p>
<img src="cad-screen.png" alt="Windows配布版で改訂モデルを開いた実画面"><p>上は実際のWindows CAD表示。CAD本体は前回の検証済み配布版と同じ実行ファイルで、今回修正したのは模型の生成と検証です。製品全体試験を今回再実行したという意味ではありません。</p></section>
<section><h2>資料と未確認範囲</h2><p><a href="{PHOTO}">同車N15の屋根・客扉・前面の原写真</a> ／ <a href="{MAKER}">三菱電機技報1982年11月・誌面55頁のAU75G</a>。2013年の写真は訓練車時代で、営業時代のすべての細部を確定する資料ではありません。第三者写真は転載していません。</p>
<p>通常版{full['parts']}／分割版{split['parts']}部品。実印刷、接着強度、走行、台車旋回、市販動力との適合は未試験です。旧モデル、ユーザー原本、前回配布は削除・上書きしていません。</p>
<p><a href="../../out/models/{a.package.name}.zip">部位別STL・STEP・CAD一式ZIP</a></p></section></html>'''
    for target in [a.samples,a.package]:
        for path in (w/('sections'+a.run)).iterdir():shutil.copy2(path,target/path.name)
        shutil.copy2(w/('negative-old-geometry'+a.run)/'results.json',target/'contour-negative-tests.json')
        package_page=page.replace(f'<a href="../../out/models/{a.package.name}.zip">部位別STL・STEP・CAD一式ZIP</a>',
            '<a href="full-car/parts.csv">通常版部品表</a> ／ <a href="small-printer/parts.csv">分割版部品表</a>')
        (target/'index.html').write_text(page if target==a.samples else package_page,encoding='utf-8',newline='\n')
        repo=Path(__file__).resolve().parents[3]
        shutil.copy2(repo/'samples/series115-ho-new-20261024/front-detail.png',target/'audit-initial-front.png')
        audit=target/'prototype-audit.html'
        audit.write_text(audit.read_text(encoding='utf-8').replace(
            '../../samples/series115-ho-new-20261024/front-detail.png','audit-initial-front.png').replace(
            'kuha115-prototype-audit-20261024.json','prototype-audit.json').replace(
            '../../samples/series115-ho-new-20261024/index.html','audit-initial-front.png'),encoding='utf-8',newline='\n')
        readme=(target/'README.md').read_text(encoding='utf-8')
        readme=readme.replace('客扉の下端は平板による簡略表現。','客扉12枚・乗務員扉2枚は車体と同じ裾絞りを持つ断面。局所曲げ形状は写真からの模型設計値。')
        readme=readme.replace('床板、床下機器外形、スカート、連結器','スカート一体床板、床下機器外形、連結器')
        readme=readme.replace('床下部品を床板下へ接着し、','スカートは床板と一体で、別の支持柱への接着は不要。その他の床下部品を床板下へ接着し、')
        readme=readme.replace('屋根断面厚0.85 mm','屋根の目標厚1.0 mm（肩を含む0.85 mm法線サンプル検査付き）')
        readme=readme.replace('通常の弦誤差0.025 mm、通風器のみ0.006 mm。','標準STL弦誤差0.025 mm、屋根・通風器・表示箱は0.006 mm、前照灯の薄い反射鏡/レンズは0.001 mm。')
        readme=readme.replace('ばねは巻線の段を表した形で、完全ならせんではない。','台車は前回検証済みの同一8部品BRepを配置。編集原本はtr62-native.kcd2。')
        readme=readme.replace('車番レタリングは収録していない。','普通幕・車番は参照画像/印刷用デカールとして保持。')
        readme='''# 2026-10-26 外形修正版

クーラーは屋根に沿う連続キセ、スカートは床板と一体印刷、14枚の扉葉は下部裾絞りを持つ。
模型の製作上の一体化と、実車の金具・溶接構造の忠実な再現は別である。
比較画像と根拠はindex.html、実BRepの検査はvalidation-summary.jsonを参照。

'''+readme
        (target/'README.md').write_text(readme,encoding='utf-8',newline='\n')
    print('Contoured model delivered:',a.samples,a.package)


if __name__=='__main__':main()
