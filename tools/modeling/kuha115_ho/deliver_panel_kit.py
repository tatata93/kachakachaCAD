"""Collect accepted models, immutable evidence and an illustrated assembly guide."""
from pathlib import Path
import argparse,datetime,hashlib,html,json,shutil,zipfile


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def load(p):return json.loads(p.read_text(encoding='utf-8'))
def write(p,d):p.write_text(json.dumps(d,ensure_ascii=False,indent=2),encoding='utf-8',newline='\n')


ROUNDS=[
 ('前頭部と屋根の段差','長手Rを9から6.3 mmへ改訂。折妻との最大継目段差を約0.415 mmから0.00008 mm未満へ。モデル内の整合値であり実車精度ではない。'),
 ('屋根肩の丸み','車幅・軒・頂点高さを保ち、横断Rを40から48 mmへ。歩み板等のSTL分割精度も上げた。局所Rは写真からの設計値。'),
 ('行先表示の奥まり','表示膜を枠の口元へ移動し、厚みと有限接触を確保。文字画像も表示面へ追従。'),
 ('灯具と外板の隙間','前照灯・尾灯の筒を実際の折妻外板まで延ばして切り合わせ。4か所の接触と内外分類を確認。'),
 ('灯具内部の取り付き','金属リングの0.02 mm隙間を解消。レンズの差込み縁と反射板の座を追加し、各材料を別部品のまま保持。'),
 ('窓と扉の細すぎる柱','左右計4か所の固定窓を0.8 mm移動し、最小柱幅を0.025から0.825 mmへ。隣の窓との干渉も確認。'),
 ('クーラーと歩み板','クーラー下縁の屋根への納まりを維持。歩み板に0.3 mmの板厚を確保し、8脚の接触を確認。'),
 ('屋根の位置決め','左右5か所ずつに一体の案内脚。側面の設計隙間0.2 mm。屋根は単一閉ソリッドを維持。'),
 ('床板の受け','左右3か所ずつに内向きの受け座を追加。6か所を個別に接触検査し、室内トレーとの隙間を確保。'),
 ('室内床の浮き','室内床の高さを変えず、8か所に支持パッド。床板への接触面と中心点の内外分類を確認。'),
]


def main():
    ap=argparse.ArgumentParser();ap.add_argument('work',type=Path);ap.add_argument('output',type=Path);a=ap.parse_args()
    w=a.work.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    (out/'.gitattributes').write_text('# Preserve byte-exact evidence and recorded SHA; allow original CRLF as data.\n*.json -text whitespace=trailing-space,space-before-tab,cr-at-eol\n*.csv -text whitespace=trailing-space,space-before-tab,cr-at-eol\n*.kcd2 binary\n',encoding='utf-8',newline='\n')
    repo=Path(__file__).resolve().parents[3]
    def copy(src,dst):
        target=out/dst;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,target);assert sha(src)==sha(target)
    variants=[('main','final-main','round10','kuha115-detailed.kcd2'),('panels','final-panel03','panel02','kuha115-panel-kit.kcd2'),
              ('compact','final-compact03','compact03','kuha115-panel-kit-compact.kcd2')]
    results={};reports={}
    for name,final,source,filename in variants:
        f=w/final;r=load(f/'result.json');assert r['passed'] and r['parts']>0
        proof=load(f/'reopen/validation.json');model=f/'cad-serialized.kcd2'
        assert proof['passed'] and proof['modelSha']==sha(model)
        report=load(f/'exports/print-parts.json');assert not report['failures'] and not report['previewOnly'] and not report['checkOnly']
        assert r['reportSha256']==sha(f/'exports/print-parts.json') and r['stlProofSha256']==sha(f/'exports/stl-validation.json')
        assert load(f/'provenance.json')['designSha256']==sha(w/source/'candidate/design.json')
        if name!='main':assert r['canonicalOrderPreserved'] and r['canonicalArchiveSha256']==sha(model)
        copy(model,filename);copy(w/source/'candidate/design.json',name+'-design.json')
        for p in ['result.json','provenance.json','exports/print-parts.json','exports/stl-validation.json','all-pairs/print-parts.json','reopen/validation.json']:
            copy(f/p,'evidence/'+name+'/'+p)
        if (f/'serialization-order.json').exists():copy(f/'serialization-order.json','evidence/'+name+'/serialization-order.json')
        results[name]=dict(r,modelFile=filename,modelSha256=sha(model));reports[name]=report
    rounds=[]
    paths=['round01','round02c','round03c']+[f'round{n:02}' for n in range(4,11)]
    for n,folder in enumerate(paths,1):
        base=w/folder;r=load(base/'result.json');assert r['geometryPassed'] and r['visualReview']=='reviewed' and r['stage']==n
        for path,digest in r['evidence'].items():assert sha((w if n==1 else base)/path)==digest
        preview=w/'previews01' if n==1 else base/'previews';images=r.get('imageSha256') or {p.name:sha(p) for p in preview.glob('*.png')}
        assert images
        for name,digest in images.items():assert sha(preview/name)==digest;copy(preview/name,f'rounds/{n:02}/'+name)
        for p in ['result.json','source-provenance.json']:copy(base/p,f'rounds/{n:02}/'+p)
        native=w/'native01/validation.json' if n==1 else base/'native/validation.json';copy(native,f'rounds/{n:02}/native.json')
        inspection=w/'inspection01.json' if n==1 else base/'inspection.json';copy(inspection,f'rounds/{n:02}/inspection.json')
        provenance=load(base/'source-provenance.json');snapshot=base/'sources'
        source_files=provenance.get('sourceFiles',provenance.get('sources'))
        assert source_files
        archive=out/f'rounds/{n:02}/sources.zip'
        with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
            for path,digest in source_files.items():
                p=snapshot/path;assert sha(p)==digest;z.write(p,path)
        with zipfile.ZipFile(archive) as z:assert z.testzip() is None
        rounds.append(dict(stage=n,title=ROUNDS[n-1][0],change=ROUNDS[n-1][1],reviewUtc=r['reviewUtc'],images=list(images),modelSha256=r.get('modelSha256') or next(iter(r['evidence'].values())),sourceArchiveSha256=sha(archive)))
    write(out/'rounds.json',rounds)
    for p in (w/'final-previews').iterdir():
        if p.is_file():copy(p,'images/'+p.name)
    for src,relative in [('panel-previews03','panels'),('compact-previews04','compact')]:
        proof=load(w/src/'render-proof.json');key='panels' if relative=='panels' else 'compact'
        assert proof['nativeReportSha256']==results[key]['reportSha256']
        for name,digest in proof['imagesSha256'].items():assert sha(w/src/name)==digest
        for p in (w/src).iterdir():
            if p.is_file():copy(p,'images/'+relative+'/'+p.name)
    for src,name,key in [('desktop-panel','panels','panels'),('desktop-compact','compact','compact')]:
        desktop=load(w/src/'desktop.json');assert desktop['exitCode']==0 and all(desktop[k] for k in ['ready','rendered','on_screen','event_loop'])
        assert desktop['sourceSha256']==results[key]['modelSha256']
        copy(w/src/'desktop.json','evidence/'+name+'/desktop.json');copy(w/src/'desktop.json.png','images/'+name+'/cad-screen.png')
    for p in (w/'round10/candidate').iterdir():
        if p.name.startswith('tr62-') or p.suffix=='.png':copy(p,p.name)
    contract=load(w/'checker-contract11/result.json');assert contract['passed'] and len(contract['cases'])==13
    copy(w/'checker-contract11/result.json','evidence/checker-contract.json')
    negative=load(w/'reuse-negative02/result.json');assert negative['passed'] and len(negative['cases'])==10
    copy(w/'reuse-negative02/result.json','evidence/reuse-negative.json')
    packages={}
    for source,name in [('print-panels03','panels'),('print-compact03','compact')]:
        proof=load(w/source/'package-proof.json');assert not proof['physicalPrintingTested']
        report=load(w/source/'print-parts.json');assert report['sourceReportSha256']==results[name]['reportSha256']
        for p in report['parts']:
            for key in ['stl','step']:assert sha(w/source/p[key])==p[key+'Sha256']
        for p in ['parts.csv','package-proof.json','stl-validation.json']:copy(w/source/p,'evidence/'+name+'/print-'+p)
        zip_path=w/f'kuha115-ho80-{name}-20261027.zip';assert not zip_path.exists()
        with zipfile.ZipFile(zip_path,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
            for p in (w/source).rglob('*'):
                if p.is_file():z.write(p,p.relative_to(w/source).as_posix())
            z.write(out/results[name]['modelFile'],results[name]['modelFile']);z.write(out/(name+'-design.json'),'design.json')
        with zipfile.ZipFile(zip_path) as z:assert z.testzip() is None
        packages[name]=dict(proof,zipFile=zip_path.name,zipSha256=sha(zip_path),zipBytes=zip_path.stat().st_size)
    summary=dict(createdUtc=datetime.datetime.now(datetime.timezone.utc).isoformat(),models=results,packages=packages,
        sequentialRounds=len(rounds),physicalPrintingTested=False,runningTested=False,
        scope='Native numerical geometry, native save/reopen, desktop render, mesh/STEP exports and assembly-path inspection; physical fabrication untested')
    write(out/'validation-summary.json',summary)
    guide(out,rounds,results,packages)
    files={p.relative_to(out).as_posix():dict(bytes=p.stat().st_size,sha256=sha(p)) for p in out.rglob('*') if p.is_file()}
    write(out/'delivery-files.json',dict(files=files,excludesSelf=True))
    print(json.dumps(dict(files=len(files),bytes=sum(x['bytes'] for x in files.values()),models={k:v['parts'] for k,v in results.items()})),flush=True)


def guide(out,rounds,results,packages):
    e=html.escape
    css='body{max-width:1200px;margin:28px auto;padding:0 24px;font:17px/1.8 "Yu Gothic UI",sans-serif;color:#20323e;background:#eef2f4}section,aside{background:white;padding:24px;margin:24px 0}h1,h2,h3{line-height:1.5}img{max-width:100%;height:auto}figure{margin:12px 0}a{color:#065f8b}.pair{display:grid;grid-template-columns:1fr 1fr;gap:20px}table{border-collapse:collapse;width:100%;font-size:15px}td,th{border:1px solid #adc0c8;padding:8px;text-align:left}th{background:#e8eef2}code{background:#e9eef0;overflow-wrap:anywhere}@media(max-width:740px){.pair{display:block}}'
    body=['<!doctype html><html lang="ja"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>115系1/80・10回改訂と板組キット</title><style>'+css+'</style>',
      '<h1>クハ115-1000 長野色・1/80 HO16.5<br>10回の形状改訂と板組キット</h1>',
      '<p>前面・屋根と取り付きの隙間を10段階で修正し、その後に板組版を作りました。通常板組版と小型分割版について、実形状・保存再読込・出力・数値的な組立経路を検査しています。</p>',
      '<aside><strong>先にCADを起動する</strong><p><a href="../../out/releases/face-input-final2-accepted-20261027/package/kachakacha_cad_next.exe">面作成を復旧したWindows版</a>。<a href="../../docs/development/face-input-recovery-20261027.html">修正内容・全ツール試験</a>。起動後は「ファイル→開く」で以下のKCD2を開きます。検証用の重いモデルは初回読込に約3分かかります。</p></aside>',
      '<section><h2>どのモデルを開くか</h2><table><tr><th>用途</th><th>ファイル</th><th>可視部品</th></tr>']
    for key,label in [('main','細部を個別に編集する元モデル'),('panels','長い造形領域向けの板組版'),('compact','小型造形機向けの前後分割版')]:
        r=results[key];body.append(f'<tr><td>{label}</td><td><a href="{e(r["modelFile"])}">{e(r["modelFile"])}</a></td><td>{r["parts"]}</td></tr>')
    body+=['</table><p>車体長243.75 mm、連結面間250 mm、最大幅36.875 mm、屋根高さ45.675 mm、台車中心間172.5 mm、軌間16.5 mm。局所R・取付隙間・細部寸法は写真に基づく模型設計値で、特定車番の実測復元ではありません。</p><img src="images/overview.png" alt="実CAD出力による全景"><p>塗装された画像は、実CADの出力STLからの描画です。予想画像やCAD操作画面ではありません。文字・色は塗装の目安です。</p></section>',
      '<section><h2>外観と部位の確認</h2><div class="pair"><img src="images/front-detail.png" alt="前面上部と灯具"><img src="images/front-profile.png" alt="前頭部の側面形状"></div><p>貫通扉窓は横7.2×縦5.8 mmを維持。額の長手R6.3 mm、屋根横断R48 mm、隅R3.8 mmは写真から選んだ模型設計値です。継目段差の縮小はモデル内の整合性であり、実車との寸法誤差を表しません。</p><img src="images/driver-side.png" alt="運転士側"><img src="images/assistant-side.png" alt="助手側"><img src="images/top.png" alt="屋根配置"><div class="pair"><img src="images/bogie.png" alt="TR62台車"><img src="images/bogie-underside.png" alt="台車裏面"></div><p>台車は既存のTR62別モデルを維持。輪軸・車輪・押え等を分けてあります。市販輪軸への置換、軸受摩擦、曲線通過、集電・走行は実物未試験です。</p></section>',
      '<section><h2>10回の観察→修正→実形状検査</h2><p>異なる10版を順に生成して確認した記録です。各回は重点検査。最終板組版のSTEP・全対干渉・保存・組立経路検査は後段で行っています。時刻は日本時間です。</p>']
    for r in rounds:
        when=datetime.datetime.fromisoformat(r['reviewUtc']).astimezone(datetime.timezone(datetime.timedelta(hours=9))).strftime('%m/%d %H:%M:%S')
        body.append(f'<h3>{r["stage"]}回目：{e(r["title"])}</h3><p>{e(r["change"])} 確認 {when}。</p><div class="pair">')
        for image in r['images'][:2]:body.append(f'<img loading="lazy" src="rounds/{r["stage"]:02}/{e(image)}" alt="第{r["stage"]}回の実形状確認">')
        body.append(f'</div><p><a href="rounds/{r["stage"]:02}/result.json">結果と画像SHA</a> ／ <a href="rounds/{r["stage"]:02}/sources.zip">その回の生成・検査ソース</a></p>')
    body+=['</section><section><h2>板組部品の構成</h2><img src="images/panels/panels.png" alt="主部品の分解図"><p>前妻・後妻・左右側板・屋根・床板の6部品を主構造とし、室内床、冷房装置、透明材、台車、連結器、後付け小部品を分けます。図の部品移動は分解表示のためで、製造STLの形を変更していません。</p>',
      '<div class="pair"><img src="images/panels/front-panel.png" alt="窓穴を持つ前妻"><img src="images/panels/assembled-shell.png" alt="外板を組んだ状態"></div><p>前妻はX=9.2 mmまで側面へ回り込み、パノラミック窓を途中で切りません。後妻はX=242.75 mmから。扉の下部には隠れる接続部を設け、扉・窓枠と外板を一つの印刷部品にしています。灯具や細い取手を無理に全部一体化していません。</p>',
      '<h3>小型版は窓や扉を避けて分割</h3><img src="images/compact/panels.png" alt="小型版の分割構成"><img src="images/compact/backers.png" alt="5枚の裏側の添え板"><p>側板・側面透明材はX=117.8 mm、屋根・床板・室内床はX=121.875 mmで分割。屋根・床板・室内・左右側板用の5枚の添え板を付属します。各添え板と両側部材の接触面を個別に検査。初回に見つかった室内添え板の0.1 mm浮きも修正済みです。</p>',
      '<h3>造形ファイルと帯材・金属線の区別</h3><table><tr><th>版</th><th>造形候補</th><th>参照部品</th><th>造形候補の最大長</th><th>出力</th></tr>']
    for key,label in [('panels','通常板組'),('compact','小型分割')]:
        p=packages[key];body.append(f'<tr><td>{label}</td><td>{p["printParts"]}</td><td>{p["referenceParts"]}</td><td>{p["maximumPrintExtentMm"]:.2f} mm</td><td><a href="../../out/models/kuha115-tenround-20261027/{p["zipFile"]}">STL・STEP・CAD ZIP</a> ／ <a href="evidence/{key}/print-parts.csv">部品表</a></td></tr>')
    body+=['</table><p>ZIPの <code>print/</code> が造形候補、<code>reference-only/</code> は金属線・プラ帯・塗装・灯具への置換用。235.75 mmの雨樋2本は帯材、細い扉手掛け12個等は金属線・薄板を想定し、小型機の125 mm制約へ混ぜていません。元の形と位置はCADと参照STEPに残しています。可視部品数は、その全てを印刷するという意味ではありません。</p>',
      '<p>通常版は244.6 mmの部品に支持と配置余白が必要です。小型版の印刷候補は125 mm未満ですが、支持の余白を含めた造形領域は別です。135 mm以上は仮の計画値で、実際には使用機とスライサーで確認してください。印刷時は単位mm・倍率100%。STLは部品ごとに造形方向へ回転・底面をZ=0に移動し、STEPはCADの組立座標を維持しています。</p>',
      '<table><tr><th>部品</th><th>STLの姿勢</th><th>工作上の確認</th></tr><tr><td>左右側板・妻板</td><td>外観側を上</td><td>裏面と裾絞り、回込部を支持。窓縁の支持痕を避ける。</td></tr><tr><td>屋根</td><td>外側を上</td><td>内側を支持。長手方向の反りを確認する。</td></tr><tr><td>床板</td><td>床上面を下、床下機器を上</td><td>平面とスカートの形を確認する。</td></tr><tr><td>透明材</td><td>側面は窓面を上</td><td>段付き立体なので平板PET一枚物の型紙ではない。</td></tr><tr><td>小部品</td><td>基本座標を底面合わせ</td><td>薄肉・最小径・支持位置を個別に決める。</td></tr></table>',
      '<p>支持材は生成していません。スライサーでの支持配置、プリンター固有の補正、実印刷、収縮・反り、支持除去は未検証です。まず短い窓周辺・屋根脚・受け座を切り出して試作し、適合を調整してから全長部品を作る手順です。</p></section>',
      '<section><h2>組立手順</h2><ol><li>部品表の名称とCADの部位別グループを照合。裏側の支持を除去して洗浄・硬化し、窓の開口を確認します。</li><li>小型版は、側板・屋根・床板・室内床をそれぞれ添え板で前後接合。平らな治具で反りと段差を抑え、接着剤が外観面に回らない量で固定します。</li><li>前妻、左右側板、後妻を乾式仮組み。回込窓の端に継目が来ない向きで合わせ、屋根と床板も仮にはめて幅・直角・対角を確認します。</li><li>車体4枚を接着。屋根を外して窓枠・車体・屋根・クーラーを別々に塗装します。屋根脚の側面は設計隙間0.2 mmですが、樹脂と塗膜で変わるので無理に押し込みません。</li><li>ガラスを内側から接着。側面透明材は段付きの一体レジン部品として設計。平板材料を使う場合は窓ごとに切り分け、各窓の深さへ合わせます。前面の曲面ガラスは別部品です。</li><li>床板へ室内床を支持パッド8か所で載せ、側壁の受け座6か所へ合わせます。床板・室内を下側から入れ、透明材や添え板に当たらないか確認して固定方法を決めます。</li><li>屋根は上から案内脚で位置決めし、クーラーを屋根に沿わせて載せます。スカートは床板に一体で、車体から細い柱で吊る構成ではありません。</li><li>台車、輪軸、押え、連結器、灯具、手掛け、雨樋を取り付けます。別部品の接着箇所、輪軸の転がり、車輪と台枠の隙間を実物で確認して完成させます。</li></ol>',
      '<img src="images/panels/glazing.png" alt="左右の段付き透明材"><p>この画像は透明材の形の説明用です。レジンの光学透明性や塗装後の完成状態を保証する画像ではありません。</p></section>',
      '<section><h2>最終検証</h2><table><tr><th>版</th><th>部品</th><th>点分類</th><th>接触</th><th>再結合</th><th>挿入経路</th><th>全対→BRep候補</th></tr>']
    for key,label in [('main','編集元'),('panels','通常板組'),('compact','小型分割')]:
        r=results[key];body.append(f'<tr><td>{label}</td><td>{r["parts"]}</td><td>{r["pointChecks"]}</td><td>{r["attachmentChecks"]}</td><td>{r["splitChecks"]}</td><td>{r["insertionChecks"]}</td><td>{r["totalPairs"]} → {r["brepPairs"]}</td></tr>')
    body+=['</table><p>全可視部品のBRep有効性・単一閉ソリッド・正体積、全STEP再読込、STL閉鎖性・面向き・退化・体積差、全対の境界箱とBRep干渉、接触面積、保存再読込を検査。STL体積差上限0.2%、干渉許容1e-6 mm³を維持しました。</p>',
      '<p>屋根は上へ20 mm、床板と室内は下へ45 mm外す経路を、透明材と小型添え板を含めて検査。逆順を組立順とします。形状を動かした数点だけで成功扱いせず、モデル公差内の連続掃引に基づく数値的判定です。数学的な厳密証明や実機のはめ合い試験ではありません。</p>',
      '<p><a href="validation-summary.json">対象版・結果の一覧</a> ／ <a href="evidence/checker-contract.json">検査器の正負13条件</a> ／ <a href="evidence/reuse-negative.json">古い証拠・改変STEP/STL等を拒否する10条件</a> ／ <a href="delivery-files.json">配布ファイルのSHAとサイズ</a>。途中失敗は元の作業フォルダーに保全し、成功結果へ書き換えていません。板組版の生成時の2/1グループの並びはCAD登録順へ正規化し、配布にはCADが保存したファイルを使用。その並びの再読込維持も確認しています。</p>',
      '<div class="pair"><figure><img src="images/panels/cad-screen.png" alt="Windowsの通常板組版CAD画面"><figcaption>配布CADで通常起動した板組版。色付き完成図とは異なる実操作画面。</figcaption></figure><figure><img src="images/compact/cad-screen.png" alt="Windowsの小型分割版CAD画面"><figcaption>小型分割版の通常表示。</figcaption></figure></div>',
      '<p>今回の面作成修正は195 CTest、開発／配布UI各608件、旧自己試験、通常Windows起動を完了。その後の模型生成変更ではアプリ本体を変えていません。モデルの初回読込は重く、通常板組版で約176.5秒。詳細版のキャッシュ再利用試験は期待したhitを確認できず、不合格記録を保全しています。モデルが表示できたことと、高速読込を達成したことは区別しています。</p></section>',
      '<section><h2>資料と未確認事項</h2><p><a href="https://kokuden-library.com/2025/12/22/jr%E6%9D%B1%E6%97%A5%E6%9C%AC115%E7%B3%BB1000%E7%95%AA%E5%8F%B0-%E9%95%B7%E3%83%8A%E3%83%8En15%E7%B7%A8%E6%88%90%EF%BC%88%E8%A8%93%E7%B7%B4%E8%BB%8A%EF%BC%89/">クハ115-1222／N15の実車写真</a>で前面・屋根・左右側面を照合。2013年訓練車の写真は形状資料で、長野色営業時代の標記確定資料ではありません。</p><p><a href="https://www.greenmax.co.jp/gm-product/414.html">GreenMax エコノミーキット</a>と<a href="https://www.greenmax.co.jp/pdf/gm-instructions/gm-instructions-414.pdf">公式組立説明</a>は板組構成の参考。Nの300番台製品をHO1000番台の形状根拠や寸法互換として使っていません。</p><p>特定年度・車番の全細部、床下配管と表示位置の完全同定、実車の局所Rの実測、実造形、接着強度、耐久性、走行は未確認です。必要な材料・支持・はめ合いを考慮したCAD設計と数値検証までを今回の範囲とし、実際に印刷・組立済みとは表示しません。元モデル・旧版・ユーザーの未コミットモデルは保全しています。</p></section></html>']
    (out/'index.html').write_text('\n'.join(body),encoding='utf-8',newline='\n')


if __name__=='__main__':main()
