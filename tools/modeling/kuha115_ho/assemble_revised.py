"""Package the revised model and its verified evidence; no prototype photos copied."""
from pathlib import Path
import argparse, html, json, shutil, sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'series115_print'))
from package_print import arrange
from review import sha


def main():
    ap=argparse.ArgumentParser();ap.add_argument('workspace',type=Path);ap.add_argument('samples',type=Path)
    ap.add_argument('package',type=Path);ap.add_argument('--run',default='04');a=ap.parse_args();w=a.workspace;run=a.run
    proof=json.loads((w/'review-result.json').read_text(encoding='utf-8'))
    assert proof['passed'] and len(proof['models'])==2
    negative=json.loads((w/'review-negative-tests.json').read_text(encoding='utf-8'))
    candidate_design=json.loads((w/('candidate'+run)/'design.json').read_text(encoding='utf-8'))
    fitted=candidate_design.get('attachmentRevision')
    expected_negative=8 if candidate_design.get('contours') else 7 if fitted else 4
    assert negative['passed'] and len(negative['rejected'])==expected_negative and negative['reviewResultSha256']==sha(w/'review-result.json')
    a.samples.mkdir(parents=True,exist_ok=False);a.package.mkdir(parents=True,exist_ok=False)
    for path in (w/('candidate'+run)).iterdir():
        if '-all-pairs' not in path.name:shutil.copy2(path,a.samples/path.name)
    for path in (w/('previews'+run)).glob('*.png'):shutil.copy2(path,a.samples/path.name)
    for split in [False,True]:
        prefix='split-' if split else '';suffix='-split' if split else ''
        shutil.copy2(w/(prefix+'native'+run)/'cad-serialized.kcd2',a.samples/('kuha115-ho80-revised'+suffix+'-saved.kcd2'))
        shutil.copy2(w/(prefix+'exports'+run)/'print-parts.json',a.samples/('validation'+suffix+'.json'))
        shutil.copy2(w/(prefix+'all-pairs'+run)/'print-parts.json',a.samples/('interference'+suffix+'.json'))
    shutil.copy2(w/('desktop'+run)/'desktop.json.png',a.samples/'cad-screen.png')
    shutil.copy2(w/'review-result.json',a.samples/'validation-summary.json')
    shutil.copy2(w/'review-negative-tests.json',a.samples/'review-negative-tests.json')
    repo=Path(__file__).resolve().parents[3]
    for ext in ['html','json']:
        shutil.copy2(repo/('docs/development/kuha115-prototype-audit-20261024.'+ext),a.samples/('prototype-audit.'+ext))
    full,split=proof['models'];counts=f"通常版{full['parts']}部品／前後分割版{split['parts']}部品"
    readme=f'''# クハ115 長野色・調査反映モデル — 1/80 HO16.5

貫通扉窓を**幅7.2×高さ5.8 mm（幅/高さ1.241）**の横長丸角形にした。
Hゴム外周は7.6×6.2 mm。扉全高22.9 mmとは独立した寸法で、開口・ゴム・ガラスを同じ設計値から作る。
この数値は写真とユーザーの形状指示に基づく模型設計値であり、実測確定値ではない。

## 開くファイル

- `kuha115-ho80-revised-saved.kcd2`：CAD自身が保存した通常版。まずこちらを開く。
- `kuha115-ho80-revised-split-saved.kcd2`：CAD保存の前後分割版。
- `kuha115-ho80-revised.kcd2` / `kuha115-ho80-revised-split.kcd2`：生成元のネイティブ履歴文書。
- `design.json` / `design-split.json`：寸法・色・部品UUID・検査対象。
- `index.html`：実形状画像、実際のCAD画面、検査結果の一覧。

{counts}。以前の列車文書を読み込まず、ワイヤー・押し出し・ロフト・Booleanを新たに構築した。
汎用の文書生成ヘルパーだけ既存コードを利用する。編集の正本はネイティブCAD履歴で、STLではない。
CADの「ファイル → 開く」で文書を指定する。保存済み版と生成元版は同じ文書IDの代替ファイルなので、
両方を別モデルとして読み込んで結合する用途には使わない。

## 反映した形状

- 浅い折妻、前面隅の回込み、裾絞り、肩から中央へ接線でつながる屋根断面、前頭ドーム。
- 横長の貫通扉窓、黒Hゴム、前面から側面へ回るガラス、銀色の細い縦桟、ワイパー。
- 独立した運転室側窓・乗務員扉、片側3か所の客扉、固定窓と上下サッシの区別、便所側の左右差。
- AU75Gの2ファン、開口と内部仕切のある6個の通風器、歩み板、無線アンテナ、信号炎管。
- TR62系の開いた側枠、軸箱、軸ばね、枕ばね、上下枕梁、ダンパー、リンク、内側ディスク、前台車プロウ。
- 床板、床下機器外形、スカート、連結器、室内座席と運転室仕切・便所区画。

外装候補はクハ115-1222／N15の長野色営業時代（1994〜97年頃）。ユーザーは車番・年度を固定していない。
2013年の同車写真は訓練車・湘南色であり、外板資料として使い、当時の塗装・ロゴを移植していない。
床下機器の同定、局所R・折角・窓寸法、細部位置には写真推定が残る。車番レタリングは収録していない。
客扉の下端は平板による簡略表現。ばねは巻線の段を表した形で、完全ならせんではない。

## 部位グループと組立

左ペインは01車体、02屋根、03サッシ、04透明板、05扉・手すり・雨樋、06前頭部・後妻、
07屋根機器、08床板、09床下、10台車・輪軸、11室内に分けた。分割版のみ12分割部品を加える。

1. 印刷範囲に応じて通常版か分割版を選ぶ。分割版はX=121.875で車体・屋根全体・床板・室内・左右雨樋を前後に分ける。
   前頭ドームだけの別部品ではない。床板継ぎ目に添え板を接着する。最大部品寸法は{split['maximumPartExtentMm']:.3f} mm。
2. 車体と屋根を塗装し、サッシ・Hゴム・扉を外側から、透明板を内側から位置合わせして接着する。
   曲面ガラスは前面隅に沿う別部品。表示板・小手すり等は現物の塗膜・収縮を見て調整する。
3. 屋根機器は屋根曲面で底を切った形。車体と屋根は接着、または製作者が着脱機構を設計する。
4. 床下部品を床板下へ接着し、室内トレーを載せる。台車の径2.2 mm中心穴へM2ねじ等を用意する。
   床板にねじ山・ナット保持部は作っていない。ねじ長、座金、ナットの収まりは製作時に決める。
5. 輪軸を台車下面の溝から入れ、押え板を取り付ける。軸を接着して固定しない。
   HO16.5用の外観・機構案であり、市販輪軸互換、絶縁、通電、走行・曲線通過は実機未試験。

車体標準厚1.0 mm、屋根断面厚0.85 mm、床板厚1.3 mm、透明板厚0.4 mm。
手すり・縦桟など0.26〜0.34 mmの細部を含む光造形向け設計。全てをFDMで印刷できる保証はしない。
STLは各部品のXY中央・最低Z=0へ平行移動したもの。支持材や最適印刷角は未設定。
STEPは組立座標のまま。ZIP内の`full-car/parts.csv`と`small-printer/parts.csv`に部位・寸法・色名を記録する。

## レビューと検証

前頭屋根の異常なロフト膨張、台車ばね周辺のBoolean不成立、屋根機器の下側残材、
プロウと車輪・室内と外板の干渉を直した。さらに全可視部品対の候補検査から、
手すり脚・縦桟・渡り板・取付座の食込みを修正し、ワイパー・締結耳・ジャンパ栓は接続して一体化した。

Windows配布済みCADで両版を生成。全{full['parts']+split['parts']}部品が各1閉ソリッド・有効BRep・正体積。
全STEPを再読込、全STLを閉鎖性・面向き・退化・体積差で検査した。STLの体積差上限は0.2%のまま。
通常の弦誤差0.025 mm、通風器のみ0.006 mm。分割版6組は元形状との再組立比較に成功した。
各版{full['pointChecks']}点で窓の開口や柱を確認した。
全可視部品対のAABBを1e-5 mmのすき間まで候補化し、通常版{full['brepInterferenceCandidates']}組／
分割版{split['brepInterferenceCandidates']}組の実BRep共通体積が1e-6 mm³以下であることを確認した。
候補集合は最新の出力AABBから再計算し、古い候補リストを受け付けない。
これは静止した組立位置の検査で、台車を旋回した状態の動的干渉検査ではない。

CAD保存後のUUID・定義・所属・表示状態・グループ階層・材質を比較し、通常版を再読込して
全可視部品の体積と境界箱を再比較した。最大体積差は{proof['savedReopen']['maximumVolumeDeltaMm3']:.3g} mm³。
完全な円の基準軸の平面内正規化、原点作業面3枚の追加、グループ子要素の標準並び替えは
CADの保存仕様として区別し、それ以外の値・所属・状態を照合した。
実際のWindowsデスクトップでモデルが表示されることも確認した。マウスで全工程を操作した証拠ではない。
アプリ本体は変更しておらず、製品全体試験の再実行をしたという意味ではない。
寸法推定と、実印刷・組立・走行の未試験はこの幾何検査では解消していない。
SHA・件数・所要時間は`validation-summary.json`、詳細は`validation*.json`と`interference*.json`。

## 資料と寸法の確度

- 車体長19500／模型243.75 mm、最大幅2950／36.875 mm：[JR東日本主要諸元](https://www.jreast.co.jp/press/2022/20220928_ho04.pdf)。
  この設計の幅36.875 mmは雨樋外端間。側板外面間は36.25 mm、手すりはさらに外へ張り出す。
- 屋根高3654／45.675、床高1225／15.3125、連結面間20000／250 mm：[しなの鉄道会社案内](https://www.shinanorailway.co.jp/corporate/docs/company2025.pdf)。
- AU75Gの3660×2110×376／45.75×26.375×4.7 mmと2ファン：[三菱電機技報1982年11月](https://www.giho.mitsubishielectric.co.jp/giho/pdf/1982/8211.pdf)。
- TR62参考軸距2100／26.25、車輪径860／10.75 mmは原型の参考寸法。HOの横寸法は機構に合わせて調整。
- 台車中心間13800／172.5 mmは添付参考図から採用し、一次寸法資料による独立確認は未了。
- 細部の根拠と未確定項目は既存の[部位別調査](prototype-audit.html)を参照。
  調査ページ内の旧コード診断は当時の記録であり、この改訂モデルの検査結果とは区別する。

第三者の写真・PDF、AI完成予想図はこの配布モデルの実形状画像に置き換えており、同梱していない。
'''
    (a.samples/'README.md').write_text(readme,encoding='utf-8',newline='\n')
    rows=''.join('<tr>'+''.join('<td>'+html.escape(str(m[k]))+'</td>' for k in ['name','parts','assemblyChecks','pointChecks','brepInterferenceCandidates','splitReassemblyChecks'])+'</tr>' for m in proof['models'])
    page='''<!doctype html><html lang="ja"><meta charset="utf-8"><title>クハ115 長野色・横長貫通扉窓 1/80</title>
<style>body{max-width:1200px;margin:30px auto;padding:0 22px;font:17px/1.8 sans-serif;color:#20303a;background:#edf1f3}h1,h2{line-height:1.4}section{background:white;padding:24px;margin:24px 0}img{max-width:100%;height:auto}figure{margin:20px 0}a{color:#005d88}nav a{display:inline-block;padding:10px}table{border-collapse:collapse}th,td{padding:8px;border:1px solid #9baab3}.front{max-height:720px}.side{width:100%}</style>
<h1>クハ115 長野色<br>調査を反映した新規モデル・1/80 HO16.5</h1>
<p>貫通扉窓を横長に修正。開口7.2×5.8 mm、幅/高さ1.241。Hゴム・ガラスも同じ輪郭に合わせました。寸法は写真による模型設計値です。</p>
<nav><a href="kuha115-ho80-revised-saved.kcd2">通常版CADを開く</a><a href="kuha115-ho80-revised-split-saved.kcd2">前後分割版CAD</a><a href="README.md">組立と資料・限界</a><a href="validation-summary.json">検証記録</a></nav>
<figure><img src="overview.png"><figcaption>実際に生成・検査したCAD形状の塗装レンダリング。AI予想図ではありません。CADの画面キャプチャとも区別しています。</figcaption></figure>
<section><h2>横長の貫通扉窓と前頭部</h2><img class="front" src="front-detail.png"><img class="front" src="front.png"><p>扉全高22.9 mmと窓の高さを独立に設定。浅い折妻・回込窓・銀色縦桟・Hゴム・裾絞りを反映しました。</p></section>
<section><h2>左右の側面と屋根</h2><p>上：運転士側／中：助手側／下：屋根。各画像の縮尺は統一していません。</p><img class="side" src="driver-side.png"><img class="side" src="assistant-side.png"><img class="side" src="top.png"></section>
<section><h2>台車と部位別構成</h2><img src="bogie.png"><img src="exploded.png"><p>台車はTR62系の構造を模型化。部位グループごとに表示・編集・出力できます。実走行と市販輪軸の互換は未試験です。</p></section>
<section><h2>実際のCAD表示</h2><img src="cad-screen.png"><p>配布済みWindows CADの通常起動で保存済みモデルを表示した実画面。色名は製作属性に保持し、この画面では塗装レンダリングと同じ塗り分けをしていません。</p></section>
<section><h2>検査結果</h2><table><tr><th>版</th><th>部品</th><th>指定干渉組</th><th>検査点</th><th>全対の候補組</th><th>分割再組立</th></tr>'''+rows+'''</table>
<p>全BRep・全STEP再読込・全STL閉鎖性を確認。全対の候補はAABBから抽出し実形状で検査しました。通常版は保存後の再読込でも全可視部品の形状を比較しました。</p>
<p>局所寸法と床下機器の同定には推定が残ります。特定車番の実測復元、実印刷済み、走行検証済みという主張はしません。詳細はREADMEに記録しました。</p></section>
<section><h2>製作用データ</h2><p><a href="../../out/models/kuha115-ho80-revised-20261024.zip">部位別STL・STEP・CAD一式ZIP</a></p><p>分割版は最大部品寸法125 mm未満。STLは部品ごとのベッド原点、STEPは組立座標です。</p></section></html>'''
    (a.samples/'index.html').write_text(page,encoding='utf-8',newline='\n')
    arrange(w/('exports'+run),a.package/'full-car');arrange(w/('split-exports'+run),a.package/'small-printer',125)
    for path in a.samples.iterdir():shutil.copy2(path,a.package/path.name)
    page=page.replace('<a href="../../out/models/kuha115-ho80-revised-20261024.zip">部位別STL・STEP・CAD一式ZIP</a>',
                      '<a href="full-car/parts.csv">通常版部品表</a> / <a href="small-printer/parts.csv">分割版部品表</a>')
    (a.package/'index.html').write_text(page,encoding='utf-8',newline='\n')
    print(a.samples);print(a.package)


if __name__=='__main__':main()
