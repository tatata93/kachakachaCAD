"""Render complete, linked lessons. Screen references never impersonate step results."""
from pathlib import Path
from html import escape as e
import re
from lesson_foundations import FOUNDATIONS
from lesson_details import DETAILS
from lesson_walkthroughs import WALKS
from lesson_variant_notes import NOTES

HERE=Path(__file__).resolve().parent
ROOT=HERE.parent.parent

# These are complete, photographed examples. A page may also have a differently sized exercise.
EXAMPLES={
 'part.extrude':[('headlight-to-plane.html','投影したライトを中央面まで伸ばす：設定表・3D選択・終端角度・完成拡大図')],
 'wire.project_surface':[('panorama-projection.html','正面と側面の2枚からパノラミック窓を投影：5段階の実画面と練習モデル')],
 'part.trim_overlap':[('solid-trim.html','窓の中を削って境界ワイヤーを残す：入力・区画・残す側・再読込')],
 'part.split_overlap':[('solid-trim.html','面・部品・ワイヤーを交わりで分割し、残す領域を選ぶ')],
 'surface.from_lines':[('solid-trim.html','曲面上の窓穴のある面：外周と内周の実例')],
 'grid.edit':[('section-and-delete.html','常設グリッド透過・平面ワイヤのみと断面表示')],
 'workplane.create':[('section-and-delete.html','作業面で断面表示し、隠れたグリッドを見ながら描く')],
 'edit.delete':[('section-and-delete.html','依存先のある削除：消える一覧と3Dの赤色表示を確認')],
 'part.thicken':[('section-and-delete.html','元面に依存しない厚み部品を作る')]
}
PHOTO_SETS={
 'part.extrude':[('v2-headlight-lamp-faces-preview.png','準備：車体へ投影した小さなライト面。'),('v2-headlight-lamp-central-plane-pick.png','終端選択：中央面を選ぶ役割へ切り替え、候補面を確認します。'),('v2-headlight-lamp-central-plane-preview.png','下見：範囲・方向・平面延長を確認した状態。'),('v2-headlight-lamp-central-plane-detail.png','完成形：同じ生成ライトを単独表示した拡大図。')],
 'wire.project_surface':[('v2-panorama-01-two-planes.png','準備：正面と側面の別々の作図面。'),('v2-panorama-02-front-direction.png','正面側の行だけに投影方向を設定。'),('v2-panorama-03-side-direction.png','側面側の行だけに別方向を設定。'),('v2-panorama-04-preview.png','下見：正面・角R・側面を回る輪郭。'),('v2-panorama-05-result.png','確定した閉じた窓輪郭。')],
 'part.trim_overlap':[('v2-trim-window-01-input.png','加工前の対象と窓輪郭。'),('v2-trim-window-02-regions.png','分割で得られた区画。'),('v2-trim-window-03-keep-body.png','車体側を残す選択。'),('v2-trim-window-04-result.png','再読込後の穴と境界ワイヤー。')],
 'fabrication.create':[('v2-approx-outline-preview.png','屋根の別例：入力面に近似の下見を重ねて比較。'),('v2-approx-outline-result.png','屋根の別例：近似モデルを確定。'),('v2-approx-outline-pattern.png','同じ屋根の型紙。切出しと組立を別々に確認。')],
 'surface.from_lines':[('v2-dome-preview.png','立体外枠と中央断面を使う別例：ドームの下見。'),('v2-dome-result.png','同じドーム例の確定結果。'),('v2-trim-curved-window-hole.png','別の曲面例：内周を窓穴として残した面。')]
}
PHOTO_STEPS={
 'part.extrude':{1:[0],6:[1],8:[2],10:[3]},
 'wire.project_surface':{1:[0],4:[1,2],7:[3],8:[4]},
 'part.trim_overlap':{1:[0],5:[1,2],6:[3]},
 'fabrication.create':{3:[0],7:[1,2]}
}

IMMEDIATE=set('''wire.coincident wire.tangent wire.curvature wire.meet_lines wire.project wire.center_points wire.key_points wire.intersection_points wire.set_datum wire.clear_datum guide.build guide.clear guide.add_row guide.append_row guide.row_up guide.row_down guide.row_remove guide.row_reverse guide.set_method group.create group.dissolve group.rename group.set_active entity.rename fabrication.assign_role fabrication.assign_relief_cut fabrication.set_connection_scope fabrication.set_unfold_base fabrication.set_method fabrication.freeze_output part.thickness_placement derived.freeze workplane.set_active'''.split())
POINT_COMMIT=set('''draw.point draw.line draw.rectangle draw.circle draw.arc draw.bezier wire.move wire.copy wire.rotate wire.mirror wire.scale part.move part.copy part.rotate part.mirror dimension.open grid.move_origin instructions.arrow instructions.text instructions.place_scene'''.split())
DISPLAY={i for i in DETAILS if i.startswith('view.')}|{'snap.toggle','selection.activate','tools.search','help.copy_diagnostics'}
FILE={i for i in DETAILS if i.startswith('file.')}|{'instructions.save','instructions.open','instructions.model_file','instructions.image','instructions.pdf'}

def finish(ident):
    if ident in ('file.new','file.open'):
        return '未保存変更の確認が出たら内容を読み、必要なら保存します。新規は新しい文書へ、開くは選んだファイルの文書へ切り替わります。','ファイル選択や保存確認の段階では取消できます。保存していない元文書を破棄しないようにしてください。'
    if ident in ('edit.undo','edit.redo'):
        return 'キーまたは命令を1回実行するごとに履歴が1段階動きます。下見やEnterによる別の確定は不要です。','Undoの反対はRedoです。Undo後に新しい変更を確定すると、以前のRedoは続けられません。'
    if ident=='edit.delete':
        return '依存先がある時は、削除一覧と3Dの赤い対象を見て、確認画面の削除を押します。単独の線・辺ではそのまま削除される場合があります。','確認画面なら取消できます。削除済みの形状と依存先は1回のCtrl+Zで戻します。'
    if ident=='measure.open':
        return '必要な要素・点・測定位置を指定すると数値が表示されます。測定だけで形状を確定するEnter操作は不要です。','測定を消去で現在の測定を消します。保存済みの寸法注記を消す操作とは別です。'
    if ident=='grid.edit':
        return 'グリッド間隔や表示の変更を適用して、格子が変わることを確認します。形状生成の確定ボタンは使いません。','間隔や表示を元へ戻します。既存ワイヤーの位置や寸法はグリッドの変更では変わりません。'
    if ident=='dimension.open':
        return '値と駆動／参照を決めた後、3Dビューで表示位置をクリックすると確定します。配置後に別の確定ボタンを押す手順ではありません。','配置前なら入力を取り消します。保存した寸法や駆動で変わった形状はCtrl+Zで戻します。'
    if ident in ('wire.corner_chamfer','wire.corner_fillet'):
        return '対象モードを「ワイヤー内の角をまとめて加工」にし、種類・数値・この頂点だけ・番号を設定します。3Dで輪郭の1辺を選び、下見を見て下端の作成ボタンまたはEnterで確定します。','Escは入力を解除し、道具と設定を維持します。加工後の結果が違う場合はCtrl+Zで戻し、番号や条件を直します。'
    if ident in ('wire.trim','wire.extend','wire.split'):
        return '3Dで対象区間や分割位置を指して、強調を確認してクリックするとその加工を実行します。後から別の確定ボタンを探す必要はありません。','クリック前ならカーソルを外すか別ツールへ切り替えます。適用済みの加工はCtrl+Zです。'
    if ident in IMMEDIATE:
        return 'この例では必要な要素を先に選んでから命令を押します。命令の実行時に処理します。対象不足から始めた場合は選択待ちになることがあるため、画面下の案内を読みます。','図面に変更が入る前なら入力を外すか選択へ戻ります。確定済みの図面変更はCtrl+Zです。役割表や次回設定の変更と幾何の変更を区別します。'
    if ident in POINT_COMMIT:
        return '最後の点や配置位置を指定した時点で、この1回の操作が完了します。数値欄を入力するEnterと、形を完成させる操作を画面下の案内で区別します。','入力をやめる時はEscを使います。作図で直前の点を戻す場合は、画面の案内に従ってBackspaceを使います。確定後の図面変更はCtrl+Zです。'
    if ident in DISPLAY:
        return '表示の切替、または設定画面での適用を確認します。この機能の説明にない「形状の確定」は不要です。','表示を元のモードや設定へ戻します。カメラや可視状態を幾何のUndoと取り違えないでください。'
    if ident in FILE or ident.startswith('export.') or ident=='output.copy':
        return '保存・出力先と対象を確認して実行ボタンを押します。出力棚を使う場合は、対象と形式を選び、出す先を選んでから出力します。','ファイル選択中は取消できます。書出したファイルは図面のCtrl+Zでは消えません。同じ文書へ生成した形状は文書のUndoを使います。'
    if ident in ('fabrication.split_part','fabrication.merge_parts','fabrication.edit_part'):
        return '条件を入力後、分割／結合をプレビューを押し、前後の形と警告を確認します。同じ条件の下見が表示された後に「編集を確定」を押すと適用します。下見なしでの初回確定は下見作成になります。','Escは案内を出し、条件や編集案を保持します。操作を終える時は選択（V）または別の道具へ切り替えます。確定した部材編集はCtrl+Zで戻します。'
    if ident=='fabrication.create_pattern':
        return '現在の近似モデル全体を対象に「展開」を押します。基準辺指定では部材番号を指定してから展開します。','Escは条件を消さず案内を表示します。操作を終えるには選択または別の道具へ切り替えます。出力先に書いたファイルは図面のUndo対象ではありません。'
    if ident=='workplane.create':
        return '下見と作ったら作業中にするの設定を確認し、「平面を作る」を押します。下見が見えているだけでは作業面は追加されていません。','未作成は入力を取り消します。作成した面はCtrl+Zで戻します。元のワイヤーの位置は変わりません。'
    if ident.startswith('instructions.'):
        return '上記の場面／用紙ごとのボタンまたは配置クリックで操作します。モデル作成用のEnter確定と同じだと思わず、編集対象の名前を確認します。','未確定の配置・ドラッグはEscで取り消します。説明書内の確定済み編集はUndoで戻し、モデル側の履歴とは区別します。'
    return '必要な入力と数値をそろえ、下見と状態欄を確認して、右ペインの作成・適用・確定ボタンを押します。Enter対応の道具はEnterでも確定できます。','Escで未確定の入力を取り消します。道具を終えるには選択（V）か別の道具へ切り替えます。確定済みの図面変更はCtrl+Z、やり直しはCtrl+Yです。'

def entry_paths():
    """Read the real ribbon categories; search remains a common alternative."""
    text=(ROOT/'src/next/kachakacha/app/Ribbon.cpp').read_text(encoding='utf-8')
    paths={}
    specs=[('DrawingCategories','作図'),('PartCategories','部品'),('FabricationCategories','製作'),('OutputCategories','出力')]
    for func,mode in specs:
        at=text.index('const std::vector<RibbonCategory>& '+func+'()')
        end=text.index('return categories;',at)
        category=''
        for line in text[at:end].splitlines():
            cat=re.search(r'\{"[a-z_]+", "([^"]+)"',line)
            if cat:category=cat[1]
            for kind,label,ident in re.findall(r'(Tool|Extra)\("([^"]+)", "([^"]+)"\)',line):
                paths.setdefault(ident,[]).append(' → '.join([mode,category]+(['その他'] if kind=='Extra' else [])+[label]))
    for label,ident in re.findall(r'Tool\("([^"]+)","(instructions\.[^"]+)"\)',text):paths[ident]=['説明書 → '+label]
    return paths

def image(name,caption,prefix='../images/'):
    assert (HERE/'images'/name).is_file(),name
    return f'<figure><a href="{prefix}{name}"><img loading="lazy" src="{prefix}{name}" alt="{e(caption)}"></a><figcaption>{e(caption)} 画像を押すと原寸で開きます。</figcaption></figure>'

def ui_figure(ident,name,phase=''):
    return f'''<figure class="screen-map"><a href="../tool-screens/{ident}.png"><img loading="lazy" src="../tool-screens/{ident}.png" alt="{e(name)}の画面構成の参考"></a><figcaption><b>画面の読む順：</b>上部で道具 → 右ペインで現在の役割と数値 → 中央の強調 → 下部の状態・確定。右の項目が見えない時はペイン内をスクロールします。<br>この実画面は既存操作試験の一場面で、下の数値例の完成画像とは限りません。{e(phase)}</figcaption></figure>'''

def list_html(items):return '<ul>'+''.join(f'<li>{e(x)}</li>' for x in items)+'</ul>'

def render_page(row,recipe,variants,evidence,variant_figures):
    ident,name,mode,icon,shortcut,predicate,failure,guide=row
    d=DETAILS[ident];commit,cancel=finish(ident)
    paths=entry_paths().get(ident,[])
    if ident=='surface.create':paths=['作図 → 面作成 → ロフト面（右の作り方で他方式へ切替）']
    if ident in ('wire.corner_chamfer','wire.corner_fillet'):paths=['Ctrl+K → '+name+'を検索、または面取り／丸めの対象モードをワイヤー内の角へ切替']
    if ident=='measure.open':paths=['作図 → 測定 → 距離／角度／半径・直径／座標／面積']
    search_name=name
    path_html=list_html(paths)+f'<p>別の入口：<kbd>Ctrl+K</kbd> → <b>{e(search_name)}</b>を検索 → 候補の用途を確認して開きます。別モードなら「全モードから探す」をオンにします。</p>'
    prep=''.join(f'<li><a href="../learn/{key}.html">{e(FOUNDATIONS[key]["title"])}</a> — 図形の作成から順に練習できます。</li>' for key in d['basics'])
    controls=[]
    for c in d['controls']:
        field,sep,value=c.partition('＝');controls.append(f'<tr><th>{e(field)}</th><td>{e(value if sep else "上の操作で確認")}</td></tr>')
    problems=[]
    for p in d['problem']:
        symptom,sep,remedy=p.partition('→');problems.append(f'<tr><th>{e(symptom)}</th><td>{e(remedy if sep else p)}</td></tr>')
    actions=WALKS.get(ident)
    if actions:
        steps=''
        for i,(title,action,expect) in enumerate(actions,1):
            photos_here=''.join(image(*PHOTO_SETS[ident][j]) for j in PHOTO_STEPS.get(ident,{}).get(i,[]))
            steps+=f'<li id="step-{i}"><h3>{e(title)}</h3><p>{e(action)}</p><p class="checkpoint"><b>画面で確認：</b>{e(expect)}</p>{photos_here}</li>'
    else:
        steps=''
        for i,action in enumerate(recipe['actions'],1):
            check = recipe['expected'] if i == len(recipe['actions']) else (
                '使う対象・値：'+recipe['settings'] if i > 1 else
                'この操作で使う入力：'+recipe['prepare'])
            steps+=f'<li id="step-{i}"><h3>{i}　{e(action)}</h3><p class="checkpoint"><b>この段階で確認：</b>{e(check)}</p></li>' 
    entry_image=HERE/'tool-entry-screens'/f'{ident}.png'
    if entry_image.is_file():
        path_html+=f'<figure><a href="../tool-entry-screens/{ident}.png"><img loading="lazy" src="../tool-entry-screens/{ident}.png" alt="{e(name)}をボタンと同じ入口で開いた画面"></a><figcaption>対象未選択でこの機能を開いた実画面（2026-10-18）。入口・入力欄の確認用です。下の練習例の生成結果ではありません。</figcaption></figure>'
    example='' 
    if ident in EXAMPLES:
        example='<aside class="worked-example"><h2>実画面で最後までたどる作業例</h2>'+''.join(f'<p><a class="lesson-button" href="../{link}">{e(label)}</a></p>' for link,label in EXAMPLES[ident])+'</aside>'
    photos=''
    if ident in PHOTO_SETS and ident not in PHOTO_STEPS:
        photos='<section id="photos"><h2>実例の準備から完成まで</h2><p>以下は具体的な別のモデルで記録した実画面です。表示された数値と、上の練習課題の数値は区別してください。</p>'+''.join(image(n,c) for n,c in PHOTO_SETS[ident])+'</section>'
    extra=[]
    for n,v in enumerate(variants):
        title=v['name']; unavailable=v.get('unavailable',False)
        intro='この方式は現在の画面では使えません。成功例として実行しないでください。' if unavailable else 'まず主手順で入力の役割を確認し、この例の値と操作へ置き換えます。'
        assert ident in NOTES and len(NOTES[ident])==len(variants),(ident,'Every method needs its own explanation')
        extra.append(f'<section id="case-{n}" class="variant"><h3>{e(title)}</h3><p>{intro}</p><p><b>この例の準備：</b>{e(v["prepare"])}</p><p>{e(NOTES[ident][n])}</p><ol>'+''.join(f'<li>{e(s)}</li>' for s in v['steps'].split('→'))+f'</ol><p class="checkpoint"><b>結果の見分け方：</b>{e(v["result"])}</p>'+variant_figures[n]+'</section>')
    proof=''
    if ident in evidence:
        ev=evidence[ident]
        proof=f'<details><summary>参考画面の出典と確認範囲</summary><p>試験名：{e(ev["case"])}。試験全体の結果：{e(ev["case_result"])}。画像はコマンド呼出し直後で、入力待ち・下見の場合もあります。各段階の完成証拠ではありません。</p></details>'
    return f'''<!doctype html><html lang="ja"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>{e(recipe['task'])} — {e(name)}</title><link rel="stylesheet" href="../manual.css"></head><body><main class="tool-page detailed-lesson" data-tool="{ident}"><p class="breadcrumbs"><a href="../../manual.html">全機能マニュアル</a> ／ <a href="../learn/index.html">作業手順を探す</a></p><header><h1><img class="tool-icon" src="../tool-icons/{ident}.png" alt="">{e(recipe['task'])}</h1><p class="eyebrow">使う機能：{e(name)}</p><p>{e(d['explain'])}</p><div class="goal"><b>この例の完成条件</b><p>{e(recipe['expected'])}</p></div></header>{example}<div class="lesson-contents"><a href="#prepare">1 準備</a><a href="#entry">2 押す場所</a><a href="#procedure">3 操作手順</a><a href="#settings">4 設定値</a><a href="#finish">5 確定と結果</a><a href="#problems">困った時</a></div><section id="prepare"><h2>1 準備 — 元の図形から作る</h2><p>{e(recipe['prepare'])}。</p><p>まだ用意できていなければ、次の手順を先に行ってください。既にモデルがある場合も、例の座標・大きさ・面の向きを自分のモデルへ読み替えます。</p><ul>{prep}</ul><p class="notice">練習前にKCD2を別名保存します。数値の長さはmm、角度は度です。平面内のu/vと世界XYZを区別します。</p></section><section id="entry"><h2>2 使う道具を開く — 押す場所</h2>{path_html}{ui_figure(ident,name) if ident in evidence else ''}</section><section id="procedure"><h2>3 操作手順 — 一つずつ進める</h2><ol class="lesson-steps" data-procedure="{ident}">{steps}</ol></section><section id="settings"><h2>4 設定する欄と数値</h2><p>ボタン名・欄名と、今回使う値を対応させてください。項目が出ていない場合は作り方・モードが合っているか確認し、ペイン内をスクロールします。</p><table><thead><tr><th>見る欄・役割</th><th>この例の設定と意味</th></tr></thead><tbody>{''.join(controls)}</tbody></table><p><b>選択の確認：</b>役割欄の名前と3Dの強調を照合します。重なって拾いにくい時はTabで候補を替えます。全体を使う例では左一覧の要素名を選び、3Dの1辺選択と区別します。</p><figure><img loading="lazy" src="../tool-diagrams/{ident}.svg" alt="{e(name)}で入力と結果を区別する操作模式図"><figcaption>入力・操作・結果の意味をまとめた模式図。数値精度や実際の完成形を示す画像ではありません。</figcaption></figure></section><section id="finish"><h2>5 確定して結果を確認</h2><p>{e(commit)}</p><div class="checkpoint"><b>結果を確認</b><p>{e(recipe['expected'])}</p></div><p>形を作る操作では、元と結果の名前を分けて一覧で確認し、必要に応じて元を一時非表示にします。表示だけの機能では形状や要素数を変えることが目的ではありません。</p></section>{photos}<section id="undo"><h2>6 取消・やり直し・保存</h2><p>{e(cancel)}</p><p>モデルの編集正本はKCD2、説明書の編集正本はKCIです。外部へ出力するだけの機能では、出力ファイルと編集元を両方残してください。</p></section><section id="problems"><h2>進めない場合 — 症状から戻る場所を探す</h2><table><thead><tr><th>起きていること</th><th>確認して直す場所</th></tr></thead><tbody>{''.join(problems)}</tbody></table><p>この機能の入力条件：{e(failure or guide)}</p><p>計算中は処理表示を見ます。失敗が出たら、対象・入力条件を変えずに何度も押すのでなく、案内にある対象と条件を確認します。</p></section>{('<section id="variants"><h2>別の方式・条件で使う</h2>'+''.join(extra)+'</section>') if extra else ''}{proof}<footer><p>操作用の名前は上の見出し・設定表を使ってください。内部の識別子は<code>{ident}</code>です。</p><p><a href="../learn/index.html">他の作業手順を探す</a> ／ <a href="../../manual.html#cmd-{ident}">機能一覧の項目へ戻る</a></p></footer></main></body></html>'''

def build_foundations(write,check,commands):
    for key,r in FOUNDATIONS.items():
        steps=''.join(f'<section id="step-{i}"><h2>{i}　{e(t)}</h2><p>{e(a)}</p><p class="checkpoint"><b>ここまでで：</b>{e(result)}</p></section>' for i,(t,a,result) in enumerate(r['steps'],1))
        photos=''.join(image(n,c) for n,c in r['images'])
        body=f'''<!doctype html><html lang="ja"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>{e(r['title'])}</title><link rel="stylesheet" href="../manual.css"></head><body><main class="tool-page detailed-lesson"><p><a href="index.html">作業手順を探す</a> ／ <a href="../../manual.html">全機能マニュアル</a></p><h1>{e(r['title'])}</h1><p>{e(r['intro'])}</p><p class="notice">未保存の原本で試さず、KCD2を別名保存して練習してください。</p>{steps}<h2>画面の参考</h2>{photos}<footer><p>数値の入れ方だけでなく、入力した値が下見や結果へ反映されていることを確認します。</p><a href="index.html">ほかの練習手順へ</a></footer></main></body></html>'''
        write(HERE/'learn'/f'{key}.html',body,check)
    groups={}
    for row in commands:
        ident,name=row[:2];groups.setdefault(ident.split('.')[0],[]).append(f'<li><a href="../tools/{ident}.html"><b>{e(name)}</b> — {e(__import__("tool_recipes").RECIPES[ident]["task"])}</a></li>')
    foundation_links=''.join(f'<li><a href="{key}.html">{e(r["title"])}</a></li>' for key,r in FOUNDATIONS.items())
    named={'file':'保存と読込み','edit':'取消・削除・数値編集','draw':'基本作図','wire':'ワイヤーの編集と投影','view':'視点と表示','part':'部品と立体加工','surface':'面を作る・整える','guide':'面の役割表','workplane':'作業面','grid':'グリッド','instructions':'説明書','fabrication':'板材近似と型紙','export':'出力','output':'同一文書への生成','group':'グループ','measure':'測定','dimension':'寸法','image':'下絵画像','tools':'ツール検索','selection':'選択','snap':'スナップ','entity':'名前','help':'診断','derived':'独立した現在形状'}
    categories=''.join(f'<section><h2>{e(named.get(group,group))}</h2><ul class="lesson-index">{"".join(items)}</ul></section>' for group,items in groups.items())
    index=f'''<!doctype html><html lang="ja"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>作業手順から探す</title><link rel="stylesheet" href="../manual.css"></head><body><main class="tool-page detailed-lesson"><a href="../../manual.html">全機能マニュアル</a><h1>何を作るか・どう操作するかから探す</h1><p>初めて使う場合は最初の練習から始めます。各機能のページには必要な図形の準備、押す場所、設定表、確定の違い、失敗した時に戻る場所があります。</p><section><h2>実画面で最初から完成まで</h2><ul><li><a href="../panorama-projection.html">正面と側面の2枚からパノラミック窓を投影</a></li><li><a href="../headlight-to-plane.html">投影したライトを中央面と同じ角度まで押し出す</a></li><li><a href="../solid-trim.html">窓をくり抜いて境界ワイヤーを残す</a></li></ul></section><section><h2>準備用の図形から作る練習</h2><ol>{foundation_links}</ol></section>{categories}</main></body></html>'''
    write(HERE/'learn'/'index.html',index,check)
