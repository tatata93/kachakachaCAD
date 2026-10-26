"""Publish the five sequential model revisions and their exact geometry evidence."""
from pathlib import Path
import argparse,datetime,hashlib,html,json,shutil,sys,subprocess,struct
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'series115_print'))
from package_print import arrange

PHOTO='https://kokuden-library.com/2025/12/22/jr%E6%9D%B1%E6%97%A5%E6%9C%AC115%E7%B3%BB1000%E7%95%AA%E5%8F%B0-%E9%95%B7%E3%83%8A%E3%83%8En15%E7%B7%A8%E6%88%90%EF%BC%88%E8%A8%93%E7%B7%B4%E8%BB%8A%EF%BC%89/'
STAGES=[
    ('01','スカートの突出','同じ横位置で車体裾より約1.59 mm前へ出ていた。','スカートを後退。幅広い床返しと栓・管も追従。','front-profile'),
    ('02','前面の手掛けとステップ','一定Xの手掛けとステップが折妻に沿わず、端が浮いていた。','折妻に沿う傾きと短い取付脚を作り、背面を実車体で切り合わせた。','front-oblique'),
    ('03b','客扉の窓と手掛け','窓が短く、手掛けが小さな丸環。下部指掛けの初回修正は裾絞りに追従しなかった。','窓上端を保って下へ拡大。角形手掛けと上下の浅い凹部。拡大画像で見つけた膜を除去して再検査。','door-side'),
    ('04','側窓と側面行先表示','側窓上端が扉上端に近すぎ、後部の側面表示もなかった。','側窓の列を下げ、上下サッシ・開口・裏ガラスを追従。同車写真で確認できる側に表示枠を追加。','rear-side'),
    ('05','ジャンパ栓と全体の再照合','5個の栓が円柱と直管で、栓箱・蓋・下側の曲がりが乏しかった。','角丸の箱と蓋、短い曲管を各1部品へまとめ、全体・左右・屋根・台車を再確認。','front-oblique')]


def checked_copy(source,target):
    sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
    digest=sha(source);result=shutil.copy2(source,target)
    assert digest==sha(source)==sha(result),'Copy changed bytes'
    return result


def canonical_receipt_newlines(target):
    # Copied legacy receipts may use CRLF. Keep their JSON values, and bind the
    # published LF bytes so Git's text normalization cannot invalidate a seal.
    changes=[]
    for path in target.rglob('*.json'):
        before=path.read_bytes();after=before.replace(b'\r\n',b'\n')
        if after==before:continue
        assert json.loads(before)==json.loads(after)
        changes.append(dict(file=path.relative_to(target).as_posix(),beforeSha256=hashlib.sha256(before).hexdigest(),afterSha256=hashlib.sha256(after).hexdigest()))
        path.write_bytes(after)
    (target/'receipt-format.json').write_text(json.dumps(dict(passed=True,scope='LF-only newline normalization; JSON values unchanged.',changes=changes),ensure_ascii=False,indent=2),encoding='utf-8',newline='\n')


def verify_print_translation(source,target,packed):
    import numpy as np
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    original=json.loads((source/'print-parts.json').read_text(encoding='utf-8'));by_id={p['id']:p for p in original['parts']}
    assert len(packed['parts'])==len(by_id)>200;rows=[]
    def vertices(path):
        data=path.read_bytes();count=struct.unpack_from('<I',data,80)[0];assert len(data)==84+50*count
        return np.ndarray((count,12),dtype='<f4',buffer=data,offset=84,strides=(50,4)).copy()
    for p in packed['parts']:
        old=by_id[p['id']];bounds=old['bounds'];move=[-(bounds[i]+bounds[i+3])/2 for i in [0,1]]+[-bounds[2]]
        assert p['printTranslationMm']==move and sha(source/old['step'])==sha(target/p['step'])==p['stepSha']
        before,after=vertices(source/old['stl']),vertices(target/p['stl'])
        assert before.shape==after.shape and np.array_equal(before[:,:3],after[:,:3])
        expected=(before[:,3:].reshape(-1,3,3).astype(float)+np.array(move)).astype('<f4')
        assert np.array_equal(expected,after[:,3:].reshape(-1,3,3)),'Print STL has an unexpected transformation'
        assert sha(target/p['stl'])==p['stlSha']
        rows.append(dict(id=p['id'],translationMm=move,stlSha256=p['stlSha'],stepSha256=p['stepSha']))
    (target/'print-translation-validation.json').write_text(json.dumps(dict(passed=True,parts=len(rows),results=rows),indent=2),encoding='utf-8',newline='\n')


def main():
    ap=argparse.ArgumentParser();ap.add_argument('workspace',type=Path);ap.add_argument('samples',type=Path)
    ap.add_argument('package',type=Path);a=ap.parse_args();w=a.workspace
    read=lambda p:json.loads(p.read_text(encoding='utf-8'));sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    proof=read(w/'review-result.json');negative=read(w/'review-negative-tests.json');sections=read(w/'appearance-sections.json')
    assert proof['passed'] and negative['passed'] and sections['passed']
    assert proof['evidenceSha256'] and all(sha(w/path)==digest for path,digest in proof['evidenceSha256'].items()),'Changed verified artifact'
    repo=Path(__file__).resolve().parents[3]
    assert all(sha(repo/path)==digest for path,digest in proof['sourceFilesSha256'].items()),'Changed modeling source after review'
    rendering=read(w/'previews05/render-proof.json')
    assert rendering['nativeReportSha256']==sha(w/'exports05/print-parts.json')
    assert rendering['designSha256']==sha(w/'candidate05/design.json')
    assert rendering['rendererSha256']==sha(Path(__file__).with_name('render_detailed.py'))
    required={name+'.png' for name in ['overview','front-detail','front','front-profile','driver-side','assistant-side','top','exploded','bogie','bogie-side','bogie-underside']}
    assert set(rendering['imagesSha256'])==required=={p.name for p in (w/'previews05').glob('*.png')},'Missing or unexpected final previews'
    assert all(sha(w/'previews05'/name)==digest for name,digest in rendering['imagesSha256'].items()),'Changed preview bytes'
    assert len(negative['rejected'])==8 and negative['reviewResultSha256']==sha(w/'review-result.json')
    delivery=read(w/'delivery-negative-tests.json')
    assert delivery['passed'] and len(delivery['rejected'])==3 and delivery['reviewResultSha256']==sha(w/'review-result.json')
    assert sections['candidate']['reportSha256']==sha(w/'exports05/print-parts.json')
    rounds=[];previous=None
    for index,(run,title,problem,fix,view) in enumerate(STAGES,1):
        candidate=w/('candidate'+run);inspection=w/('inspection'+run);r=read(inspection/'result.json')
        assert r['passed'] and r['passNumber']==index and r['designSha256']==sha(candidate/'design.json')
        assert r['sourceModelSha256']==sha(candidate/'kuha115-ho80-revised.kcd2')
        if previous:assert (candidate/'kuha115-ho80-revised.kcd2').stat().st_mtime>previous.stat().st_mtime
        previous=inspection/'result.json'
        rounds.append(dict(number=index,run=run,title=title,problem=problem,fix=fix,comparisonView=view,
            researchSource=PHOTO,photoEstimated=True,manualReview='Primary agent viewed actual generated CAD-mesh PNGs; not a claim of mouse-driven model construction.',
            inspectionCompletedUtc=datetime.datetime.fromtimestamp(previous.stat().st_mtime,datetime.timezone.utc).isoformat(),
            inspection=r))
    assert proof['models'][0]['sourceSha256']==rounds[-1]['inspection']['sourceModelSha256']
    a.samples.mkdir(parents=True,exist_ok=False);a.package.mkdir(parents=True,exist_ok=False)
    for path in (w/'candidate05').iterdir():
        if '-all-pairs' not in path.name:checked_copy(path,a.samples/path.name)
    for path in (w/'previews05').glob('*.png'):checked_copy(path,a.samples/path.name)
    checked_copy(w/'previews05/render-proof.json',a.samples/'render-proof.json')
    for split in [False,True]:
        prefix='split-' if split else '';suffix='-split' if split else ''
        checked_copy(w/(prefix+'native05/cad-serialized.kcd2'),a.samples/('kuha115-ho80-revised'+suffix+'-saved.kcd2'))
        checked_copy(w/(prefix+'exports05/print-parts.json'),a.samples/('validation'+suffix+'.json'))
        checked_copy(w/(prefix+'all-pairs05/print-parts.json'),a.samples/('interference'+suffix+'.json'))
    for source,dest in [('review-result.json','validation-summary.json'),('review-negative-tests.json','review-negative-tests.json'),
        ('appearance-sections.json','appearance-sections.json'),('application-reuse.json','application-reuse.json'),
        ('delivery-negative-tests.json','delivery-negative-tests.json'),
        ('desktop05/desktop.json.png','cad-screen.png')]:checked_copy(w/source,a.samples/dest)
    for run in ['00']+[s[0] for s in STAGES]+['03']:
        target=a.samples/('round'+run);target.mkdir()
        for path in (w/('inspection'+run)).iterdir():
            if path.suffix in ['.png','.json']:checked_copy(path,target/path.name)
    (a.samples/'rounds.json').write_text(json.dumps(dict(passed=True,rounds=rounds),ensure_ascii=False,indent=2),encoding='utf-8',newline='\n')
    full,split=proof['models'];rows=''.join('<tr>'+''.join('<td>'+html.escape(str(m[k]))+'</td>' for k in
        ['name','parts','pointChecks','attachmentChecks','brepInterferenceCandidates','splitReassemblyChecks'])+'</tr>' for m in proof['models'])
    gallery=[];before='00'
    for run,title,problem,fix,view in STAGES:
        gallery.append(f'<section><h2>{int(run[:2])}回目：{title}</h2><p>{problem}</p><p>{fix}</p><div class="pair"><figure><img src="round{before}/{view}.png"><figcaption>直前の形</figcaption></figure><figure><img src="round{run}/{view}.png"><figcaption>修正後の実形状</figcaption></figure></div></section>');before=run
    page=f'''<!doctype html><html lang="ja"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>115系 長野色・5回の外観照合と修正</title>
<style>body{{max-width:1200px;margin:30px auto;padding:0 22px;font:17px/1.8 sans-serif;color:#263640;background:#edf1f3}}h1,h2{{line-height:1.4}}section{{padding:24px;margin:28px 0;background:white}}img{{max-width:100%;height:auto}}.pair{{display:grid;grid-template-columns:1fr 1fr;gap:16px}}figure{{margin:8px 0}}table{{border-collapse:collapse}}th,td{{padding:8px;border:1px solid #9aaab3}}a{{color:#00628d}}@media(max-width:700px){{.pair{{display:block}}}}</style>
<h1>クハ115-1000 長野色・1/80 HO16.5<br>5回の外観照合と形状修正</h1>
<p>各回で生成した形を確認し、原写真と照合して次の修正へ進みました。スカート、前面取付物、客扉、側窓、ジャンパ栓を修正。5枚の画像だけを作った記録ではなく、異なる5版のCAD計算と比較です。</p>
<p><a href="kuha115-ho80-revised-saved.kcd2">通常版CAD</a> ／ <a href="kuha115-ho80-revised-split-saved.kcd2">前後分割版CAD</a> ／ <a href="README.md">開き方・組立・制限</a> ／ <a href="rounds.json">各回の対象版・時刻・検査</a></p>
<img src="overview.png" alt="今回の実CAD形状による全景">
<p>塗装画像は、CADが出力したSTLからの描画です。AI予想図や操作画面ではありません。実際のCAD画面は下に掲載しています。</p>
{''.join(gallery)}
<section><h2>スカートの前後位置を数値でも照合</h2><p>Y=0、±6、±10、±12.7、±14 mmの9か所で、車体裾Z14 mmとスカートZ7 mmの前縁を比較。修正前の最大突出は約1.59 mm、修正後は最小でも約{sections['minimumSetbackMm']:.2f} mm後退。2 mmの移動は模型設計値で、実車の実測寸法ではありません。</p><p><a href="appearance-sections.json">実メッシュ断面と旧版を拒否する比較記録</a></p></section>
<section><h2>全体の再確認</h2><img src="driver-side.png" alt="運転士側の窓・扉・機器"><img src="assistant-side.png" alt="助手側の左右差"><img src="top.png" alt="屋根機器配置"><div class="pair"><img src="front.png" alt="正面"><img src="bogie.png" alt="TR62台車"></div><p>貫通扉窓は幅7.2×高さ5.8 mmを維持。冷房キセの屋根への納まり、床板一体スカート、扉下部の裾絞り、前回のTR62実形状も保持しています。</p></section>
<section><h2>資料・推定・残る差</h2><p><a href="{PHOTO}">同車クハ115-1222／N15の原写真</a>で前面・客扉・屋根・側面幕を照合しました。写真は2013年の訓練車時代です。営業時代の長野色へ、後年の訓練車表示や内装改造を移していません。</p><p>局所R、折角、手掛け寸法、窓高さ、栓箱の大きさと管の曲がりは写真からの模型設計です。反対側の行先表示位置、床下機器の完全な同定、年代ごとの配管差は未確定。側面表示の文字は空欄です。特定年度・車番の実測復元を完了した意味ではありません。</p></section>
<section><h2>最終版の検証</h2><table><tr><th>版</th><th>部品</th><th>点</th><th>接触</th><th>全対の干渉候補</th><th>再組立</th></tr>{rows}</table><p>全可視部品が有効な1閉ソリッド・正体積。全STEP再読込、全STLの閉鎖性・面向き・退化・体積差、全対候補の実BRep干渉、保存・再読込を検査。干渉許容1e-6 mm³、STL体積差上限0.2%を維持しました。</p><p><a href="validation-summary.json">最終検証</a> ／ <a href="review-negative-tests.json">欠落・古い証拠を拒否する8試験</a>。途中の各回は重点検査で、最終版の全体検査と区別しています。</p><img src="cad-screen.png" alt="Windows CADで開いた今回の通常版"><p>CAD本体は前回の検証済み配布版と同一。今回アプリ全体試験を再実行した意味ではありません。模型は新たに計算・出力・保存再読込・Windows通常表示を確認しています。</p><p>通常{full['parts']}／分割{split['parts']}部品。実印刷・接着強度・走行・旋回は未試験です。旧版・原本は保全。</p><p><a href="../../out/models/{a.package.name}.zip">部位別STL・STEP・CAD一式ZIP</a></p></section></html>'''
    readme=f'''# クハ115-1000 長野色・5回外観改訂（1/80 HO16.5）

「ファイル → 開く」で `kuha115-ho80-revised-saved.kcd2` を開く。小型プリンター向けは `kuha115-ho80-revised-split-saved.kcd2`。
生成元の `*-revised.kcd2` と `*-saved.kcd2` は同じ文書の代替ファイル。別車両として両方を結合しない。

## 修正と根拠

5回の対象版・原写真・画像・件数は index.html と rounds.json。第三者写真は転載していない。
スカートを2 mm後退し、床板の幅広い返しとジャンパ栓を追従。前面手掛け・ステップを折妻へ合わせた。
客扉開口は上端36.6を保って26.1〜36.6 mmへ拡大。縦長手掛けと浅い指掛け凹部を追加。
側窓列の高さと後部表示を写真へ近づけ、栓箱・蓋・曲管を作り直した。細部は写真推定の模型設計値。
車体長243.75、側板幅36.25、雨樋外端36.875、屋根高45.675、床高15.3125 mm。寸法基準は design.json。
<https://www.shinanorailway.co.jp/corporate/docs/company2025.pdf>、AU75G参考寸法は <https://www.giho.mitsubishielectric.co.jp/giho/pdf/1982/8211.pdf> 誌面55頁。

## 部品と組立

通常{full['parts']}／分割{split['parts']}部品。01車体、02屋根、03窓枠、04透明板、05扉類、06前頭、07屋根機器、08床板、09床下、10台車、11室内の部位グループ。
前後分割はX121.875 mm、最大部品寸法{split['maximumPartExtentMm']:.3f} mm。継ぎ目に添え板を接着。
1. 車体と屋根を塗装。窓枠・Hゴム・扉を外側から、ガラスを裏から接着する。
2. クーラー下面は屋根曲面へ接触。スカートは床板からの幅広い返しと一体印刷する模型設計で、実車の溶接構造の完全再現ではない。
3. 床下機器を床板下面へ接着し、室内を載せる。台車の径2.2 mm穴にM2等を用意。ねじ山・ナット保持部は未設計。
4. 輪軸は下面溝から入れ押え板を取り付ける。軸を接着固定しない。市販動力・輪軸互換や曲線通過は未試験。
車体1.0、扉0.55、ガラス0.4 mm。0.26 mm程度の細部がある光造形向け。最適方向・支持材・収縮補正は未設定。
STLは部品ごとのベッド原点、STEPは組立座標。ZIP内 full-car/parts.csv と small-printer/parts.csv に元座標と部品属性。

## 検証と限界

全STEP再読込・全STL閉鎖性・全対候補干渉・保存再読込・分割6組の再組立を確認。validation-summary.json参照。
実印刷、接着強度、走行、旋回、特定年度の完全再現は未確認。床下機器の同定・反対側の細部位置には未確定が残る。
CAD本体は既存の検証済み版。今回の変更はモデル生成・検証・資料で、アプリ全体試験を再実行したという意味ではない。
'''
    (a.samples/'README.md').write_text(readme,encoding='utf-8',newline='\n')
    (a.samples/'index.html').write_text(page,encoding='utf-8',newline='\n')
    for source,target,maximum in [(w/'exports05',a.package/'full-car',None),(w/'split-exports05',a.package/'small-printer',125)]:
        packed=arrange(source,target,maximum);verify_print_translation(source,target,packed)
        subprocess.run([sys.executable,str(Path(__file__).resolve().parents[1]/'series115_print/check_stl.py'),str(target)],check=True,timeout=300)
    for path in a.samples.iterdir():
        if path.is_dir():shutil.copytree(path,a.package/path.name,copy_function=checked_copy)
        else:checked_copy(path,a.package/path.name)
    package_page=page.replace(f'<a href="../../out/models/{a.package.name}.zip">部位別STL・STEP・CAD一式ZIP</a>',
        '<a href="full-car/parts.csv">通常版部品表</a> ／ <a href="small-printer/parts.csv">分割版部品表</a>')
    (a.package/'index.html').write_text(package_page,encoding='utf-8',newline='\n')
    for target in [a.samples,a.package]:
        canonical_receipt_newlines(target)
        files={p.relative_to(target).as_posix():sha(p) for p in target.rglob('*') if p.is_file() and p.name!='delivery-files.json'}
        (target/'delivery-files.json').write_text(json.dumps(dict(passed=True,filesSha256=files),ensure_ascii=False,indent=2),encoding='utf-8',newline='\n')
    print('Published five sequential appearance revisions',a.samples,a.package)


if __name__=='__main__':main()
