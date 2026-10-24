"""Package the reviewed new model without copying third-party reference photos."""
from pathlib import Path
import argparse, json, shutil, sys

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'series115_print'))
from package_print import arrange


SOURCES = '''
## 確認した資料と寸法の確度

|項目|実車 / 1:80模型|根拠|
|---|---|---|
|車体長|19,500 / 243.750 mm|[JR東日本資料・末尾寸法図](https://www.jreast.co.jp/press/2022/20220928_ho04.pdf)|
|車体幅|2,950 / 36.875 mm|同上|
|連結面間長|20,000 / 250.000 mm|[しなの鉄道会社案内 p14](https://www.shinanorailway.co.jp/corporate/docs/company2025.pdf)|
|屋根高・床面高|3,654 / 45.675 mm、1,225 / 15.3125 mm|同上。屋根機器は屋根高に含めない|
|AU75G本体|3,660×2,110×376 / 45.750×26.375×4.700 mm|[三菱電機技報1982年11月 PDF57頁・表1.2.1](https://www.giho.mitsubishielectric.co.jp/giho/pdf/1982/8211.pdf)|
|台車中心間|13,800 / 172.500 mm|添付図による採用。一次寸法資料での独立確認は未了|
|HO線路幅|16.5 mm|ユーザー指定。実車軌間の単純な1/80換算ではない|

AU75Gの室外ファンは製造者資料に合わせて2台とした。押込通風器は更新後の
[TOMIX公式クハ側面写真と解説](https://www.tomytec.co.jp/tomix/report/ho/115-1000_nagano/115-1000_07.html)
に合わせて6個。添付図の広窓の密な横線をルーバーとして造形しない。
前面は[メーカー前頭部写真](https://www.tomytec.co.jp/tomix/report/ho/115/image/115-1-1.jpg)
を参照し、窓・Hゴム・ガラスを側面へ連続させた。
塗装名は[KATO公式長野色の説明](https://www.katomodels.com/product/n/115kei_1000_nagano)を参照。
同製品の1994〜97年仕様と今回の更新後仕様を同一車両の記録として混同していない。

前頭部の局所曲率、窓・扉位置、帯の高さ・RGB、台車外観、軸距2,100 mm、車輪径860 mm、
床下機器の小寸法・配置は写真と参考図からの推定を含む。裾絞り、室内、機器内部は簡略化した。
未確認寸法の精度を実測値として主張しない。特定車番の銘板・番号・レタリングは未収録。
第三者の参照写真・PDFは同梱せず、ユーザー提供の参考図のみを同梱する。
'''


def main():
    ap=argparse.ArgumentParser();ap.add_argument('workspace',type=Path);ap.add_argument('previews',type=Path)
    ap.add_argument('samples',type=Path);ap.add_argument('package',type=Path);a=ap.parse_args()
    evidence=json.loads((a.workspace/'review-result.json').read_text(encoding='utf-8'))
    startup=json.loads((a.workspace/'desktop-startup.json').read_text(encoding='utf-8'))
    reopen=json.loads((a.workspace/'saved-reopen/validation.json').read_text(encoding='utf-8'))
    geometry=json.loads((a.workspace/'saved-reopen-geometry/comparison.json').read_text(encoding='utf-8'))
    utility=json.loads((a.workspace/'utility-test.json').read_text(encoding='utf-8'))
    if not all(x['passed'] for x in [evidence,reopen,geometry,utility]) or not startup['ready']:raise ValueError('missing successful evidence')
    a.samples.mkdir(parents=True,exist_ok=False);a.package.mkdir(parents=True,exist_ok=False)
    for path in (a.workspace/'delivery-model').iterdir():shutil.copy2(path,a.samples/path.name)
    for path in a.previews.glob('*.png'):shutil.copy2(path,a.samples/path.name)
    shutil.copy2(a.workspace/'desktop-startup.json.png',a.samples/'cad-screen.png')
    for key,name in [('final-check','validation-main.json'),('split-check','validation-split.json')]:
        shutil.copy2(a.workspace/key/'validation.json',a.samples/name)
    evidence['desktopStartup']=startup;evidence['savedDocumentReopen']=reopen
    evidence['savedGeometryComparison']=geometry;evidence['exporterNegativeTest']=utility
    (a.samples/'validation-summary.json').write_text(json.dumps(evidence,ensure_ascii=False,indent=2),encoding='utf-8',newline='\n')
    body='''# クハ115-1000 長野色 — 新規設計 1/80・HO16.5

既存モデルを流用せず、新しいUUIDとネイティブのワイヤー・押し出し・ロフト・Boolean履歴で作成した。
以前のモデル・ユーザー原本は変更していない。対象は黒Hゴム・AU75Gを装備した更新後の代表形。

## 開くファイル

- `kuha115-1000-ho80-new.kcd2`：通常版149部品。車体・床板などを一体長で収録。
- `kuha115-1000-ho80-new-split.kcd2`：小型プリンタ版156部品。車体・主屋根・床板・室内・左右雨樋を前後分割。
- `design.json` / `design-split.json`：寸法、色名、部品ID、検査対象。
- `reference-three-view.png`：ユーザー提供図の原画像。CAD内にも非表示の参考画像3枚を配置。
- `index.html`：画像・検査・資料をまとめた閲覧用ページ。

CADの「ファイル → 開く」で文書を指定する。左ペインの01〜13で部位別に表示を切り替える。
参考図は12の非表示グループ内。画像は初期校正の補助で、正確な寸法の正本は立体の設計値。
CADは色名・材質を属性に保存するが、部品ごとの長野色塗り分け表示には未対応。
カラー画像は検査済みSTLから作った塗装用レンダリング。`cad-screen.png`のみ実際のCAD起動画面。
手作業のマウス操作を行った証拠としては扱わない。

## 部品と組立

01車体、02屋根、03サッシ、04ガラス、05扉・手すり、06前面・後妻、07屋根機器、
08床板、09床下、10台車・輪軸、11室内、12参考画像、13小型プリンタ用分割部品。
通常版のグループ数は17（入れ子を含む）。窓や貫通口は塗装だけでなく実際に抜いている。

1. 一体長が印刷できない場合は分割版を選ぶ。最大部品寸法は125 mm未満。
   車体等はX=121.875で分割し、屋根の前頭ドームは別部品。床板継ぎ目は添え板で接着する。
2. 車体と屋根を塗装し、サッシ・Hゴムを外から、透明板を内側から接着する。
   前面の曲面ガラスは側面まで連続する。参考図に誤描写のある広窓も上下窓として造形。
3. AU75G・通風器等は屋根曲率に沿った底面を持つ。接着すき間は設計上0.05 mm。
4. 床下機器・スカート・連結器を取付座で床板に接着し、室内トレーを載せる。
5. 輪軸を台車下面のU字溝から入れ、押え板を軸へ接着しないように固定する。
   台車・床板にはM2用の径2.2 mm穴。ねじ・ナット・座金は別途用意する。
6. 床板を車体に合わせ、屋根を接着または模型工作側で着脱加工する。
   車体と床板にねじ止め機構は設けていない。塗膜と樹脂収縮に合わせて現物調整する。

車体と屋根の標準厚1.0 mm、床板1.25 mm、透明板0.45 mm。
Hゴム・サッシ・ワイパー等は0.3〜0.4 mm程度の細部を含み、光造形を想定する。
STLはXY中央・最下点Z=0へ移動しただけで、支持材・最適印刷角度は設定していない。
STEPは組立位置を保持する。部品表`parts.csv`の名称・グループ・寸法・色名を参照する。
商用台車・動力・車輪との互換、実印刷、組立、通電・走行は未試験。走行用完成設計として保証しない。

## レビューで直した点

- 床板の外周を逃がし、前面ガラスと運転室側面ガラスの重なりを修正。
- 窓と扉・貫通口の実開口を44点で検査。狭い固定窓に不要な中桟を作らない。
- AU75Gの2ファン、6個の通風器、脚・床下機器の取付座を反映。
- 台車中心穴に残っていた薄膜を除去。輪軸を下から挿入できる軸受と押え板に変更。
- 屋根の内外円弧を同じ水平合わせ面で終え、車体側壁への食込みを修正。
- Hゴムと屋根機器の合わせ面を調整し、干渉検査を62組から75組に拡充。
- 通風器のSTL体積誤差を検出し、該当6部品の弦誤差0.025から0.005 mmへ細密化。
  他部品を一律に細密化すると前頭ドームの微小面で退化三角形が生じるため、部品別に指定。許容値は緩和しない。

## 検証範囲

配布済みWindows CADによりネイティブ履歴を再生成。通常版149部品・分割版156部品を
各1ソリッド・有効BRep・正体積として確認。両版で75組の干渉検査・44点の穴/柱検査を実行。
分割版では6組の前後再組立を元の形状と比較。全STEPを再読込し、全STLの閉鎖性・
面向き・退化・体積差を検査。STL体積差上限0.2%、分割形状差は絶対1e-6 mm³または相対1e-8。
これは列挙した組の検査であり、全ての部品対を総当たりした証拠ではない。
CAD自身が保存した文書を再読込し、全149部品の体積差0と75組・44点の再検査成功を確認。
CADが自動追加する原点作業面3枚を除き、元のUUID・定義値・材質属性が保存されている。
通常のWindows画面の起動・表示も確認した。
モデルのみの変更で、アプリ本体を改修・再リリースしていない。
検証済みの既存CADを使い、製品全体試験の再実行はしていない。
厳密な値・SHA・所要時間は`validation-summary.json`、各出力の`print-parts.json`を参照。
'''+SOURCES
    (a.samples/'README.md').write_text(body,encoding='utf-8',newline='\n')
    rows=''.join('<tr>'+''.join(f'<td>{m[k]}</td>' for k in ['name','parts','assemblyChecks','pointChecks','splitReassemblyChecks'])+'</tr>' for m in evidence['models'])
    html='''<!doctype html><html lang="ja"><meta charset="utf-8"><title>クハ115-1000・1/80 HO 新規モデル</title>
<style>body{max-width:1150px;margin:30px auto;padding:0 22px;font:17px/1.8 sans-serif;color:#192c36;background:#f3f6f7}img{max-width:100%;background:white}h1,h2{line-height:1.4}a{color:#006494}table{border-collapse:collapse}td,th{border:1px solid #91a3ad;padding:8px}figure{margin:24px 0}section{background:white;padding:22px;margin:24px 0}pre{white-space:pre-wrap;overflow-wrap:anywhere}nav a{display:inline-block;padding:10px}</style>
<h1>クハ115-1000 長野色<br>新規設計 1/80・HO16.5</h1>
<p>車体長243.750 mm / 幅36.875 mm / 屋根高45.675 mm。更新後の代表形。既存モデルは流用していません。</p>
<nav><a href="kuha115-1000-ho80-new.kcd2">通常版CAD</a><a href="kuha115-1000-ho80-new-split.kcd2">分割版CAD</a><a href="README.md">組立・資料・限界</a><a href="validation-summary.json">検証記録</a></nav>
<figure><img src="overview.png"><figcaption>検査済みCAD出力からの塗装レンダリング。CAD実画面ではありません。</figcaption></figure>
<section><h2>前頭部と部品構成</h2><img src="front-detail.png"><img src="exploded.png"><p>Hゴム、側面まで回る透明板、実際に抜いた窓、上下窓サッシ、屋根・床板・台車を別部品にしました。</p></section>
<section><h2>形状確認用の三方向</h2><img src="side.png"><img src="top.png"><img src="front.png" style="max-height:600px"><p>画像は同じ立体からの正投影。各視点の画像縮尺は統一していません。</p></section>
<section><h2>実際のCAD画面</h2><img src="cad-screen.png"><p>Windowsネイティブ画面で起動・文書表示を確認。CAD内は部品ごとの塗り分け表示に未対応です。</p></section>
<section><h2>検証</h2><table><tr><th>版</th><th>部品</th><th>干渉組</th><th>開口等の点</th><th>再組立</th></tr>'''+rows+'''</table>
<p>全BRep・STEP再読込・STL閉鎖性検査が成功。実印刷・市販動力互換・走行は未試験。</p>
<p>局所曲率・窓配置・小機器には推定を含みます。特定車番の実測精密復元ではありません。出典・推定箇所と組立方法はREADMEに記録しました。</p></section>
<section><h2>ダウンロード</h2><p><a href="../../out/models/kuha115-ho80-20261024.zip">部位別STL・STEPとネイティブCAD一式ZIP</a></p><p>STLは各部品のベッド原点、STEPは組立座標。小型プリンタ版の最大部品は125 mm未満です。</p></section></html>'''
    (a.samples/'index.html').write_text(html,encoding='utf-8',newline='\n')
    arrange(a.workspace/'verified-main',a.package/'full-car')
    arrange(a.workspace/'verified-split',a.package/'small-printer',125)
    for path in a.samples.iterdir():
        if path.suffix in ['.kcd2','.png','.md','.html','.json']:shutil.copy2(path,a.package/path.name)
    html=html.replace('<a href="../../out/models/kuha115-ho80-20261024.zip">部位別STL・STEPとネイティブCAD一式ZIP</a>',
                      '<a href="full-car/parts.csv">通常版部品表</a> / <a href="small-printer/parts.csv">分割版部品表</a>')
    (a.package/'index.html').write_text(html,encoding='utf-8',newline='\n')
    print(a.samples);print(a.package)


if __name__=='__main__':main()
