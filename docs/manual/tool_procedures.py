"""Deterministic per-command illustrated instructions. Drawings are schematics, not test screenshots."""
from pathlib import Path
from html import escape as esc
import json, textwrap, base64
from tool_recipes import RECIPES
from tool_variants import VARIANTS
HERE=Path(__file__).resolve().parent

def write(path,text,check):
    if check:
        assert path.is_file() and path.read_text(encoding='utf-8')==text, f'Regenerate {path.name}'
    else:
        path.parent.mkdir(parents=True,exist_ok=True);path.write_text(text,encoding='utf-8',newline='\n')

def drawing(ident,stage):
    """Use the real tool icon as the operation, with geometric input/output symbols."""
    color='#087ea0' if stage==0 else '#d07113'; fill='none' if stage==0 else '#b9e1e8'
    prefix=ident.split('.')[0]
    shape=f'<rect x="35" y="30" width="130" height="75" rx="2" fill="{fill}" stroke="{color}" stroke-width="3"/>'
    if ident.endswith(('.circle','.arc')) or 'measure' in ident or 'dimension' in ident:
        shape=f'<circle cx="100" cy="70" r="42" fill="{fill}" stroke="{color}" stroke-width="3"/><path d="M58 125h84m-84-7v14m84-14v14" stroke="{color}"/>'
    if prefix in ('surface','guide','fabrication') or 'project' in ident or 'thicken' in ident:
        shape=f'<path d="M20 112Q30 40 75 40L170 20Q198 45 180 95Z" fill="{fill}" stroke="{color}" stroke-width="3"/>'
        if stage:
            shape+=''.join(f'<path d="M{x} 106Q{x+10} 48 {x+22} 34" fill="none" stroke="#73acb7"/>' for x in (45,70,95,120,145))
    if prefix=='part' or prefix=='export':
        shape=f'<path d="M35 48l95-25 45 32v65l-95 25-45-32Z M35 48l45 32 95-25M80 80v65" fill="{fill}" stroke="{color}" stroke-width="3"/>'
        if 'boolean_cut' in ident or 'trim' in ident:
            shape+= '<rect x="100" y="60" width="30" height="45" fill="white" stroke="#d07113" stroke-width="3"/>' if stage else '<path d="M100 45v85m30-90v80" stroke="#b45d89" stroke-width="8" opacity=".7"/>'
        if 'array' in ident and stage:shape=f'<g transform="scale(.45)">{shape}<g transform="translate(220,0)">{shape}</g><g transform="translate(440,0)">{shape}</g></g>'
    if prefix=='workplane' or prefix=='grid':
        shape='<path d="M20 100L130 25l70 45-110 75Z" fill="#d4e6f6" stroke="#526ec0" stroke-width="3"/>'
        shape+=''.join(f'<path d="M{x} {100-(x-20)*.68}l70 45" stroke="#95b5ce"/>' for x in range(35,131,20))
    if prefix in ('instructions','file','output'):
        shape='<path d="M50 15h90l30 30v110H50Z" fill="white" stroke="#597086" stroke-width="3"/><path d="M140 15v30h30" fill="none" stroke="#597086"/>'
        if stage:shape+='<rect x="65" y="55" width="85" height="48" fill="#b9e1e8"/><path d="M65 117h85m-85 14h65" stroke="#597086" stroke-width="3"/>'
    if 'delete' in ident and stage:shape='<path d="M50 40l90 90m0-90-90 90" stroke="#bd3030" stroke-width="8"/>'
    return shape

def diagram(ident,title,settings,unavailable=False):
    # Japanese stays as text so users can search/copy the settings. Geometry is illustrative only.
    icon_data=base64.b64encode((HERE/'tool-icons'/f'{ident}.png').read_bytes()).decode('ascii')
    lines=textwrap.wrap(settings,42)[:3]
    text=''.join(f'<text x="24" y="{240+i*25}" font-size="17">{esc(t)}</text>' for i,t in enumerate(lines))
    result='未対応・代替を確認' if unavailable else '期待する結果（模式図）'
    right='<path d="M60 40l80 80m0-80-80 80" stroke="#bd3030" stroke-width="7"/>' if unavailable else drawing(ident,1)
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="840" height="330" viewBox="0 0 840 330"><title>{esc(title)}</title><rect width="840" height="330" fill="#f6f9fb"/><g font-family="Yu Gothic UI,Meiryo,sans-serif" fill="#203043"><text x="24" y="30" font-size="20">入力・元形状</text><text x="332" y="30" font-size="20">操作</text><text x="555" y="30" font-size="20">{result}</text><g transform="translate(25 42)">{drawing(ident,0)}</g><path d="M260 116h55m-12-10 12 10-12 10M460 116h55m-12-10 12 10-12 10" fill="none" stroke="#597086" stroke-width="4"/><image href="data:image/png;base64,{icon_data}" x="345" y="68" width="96" height="96"/><g transform="translate(565 42)">{right}</g>{text}<text x="24" y="315" font-size="14" fill="#526777">操作説明用の模式図。縮尺・曲率・実際の生成結果を示す検証画像ではありません。</text></g></svg>'''

def build_pages(commands,check=False):
    assert set(RECIPES)=={r[0] for r in commands},'Every command needs an explicit recipe; no generic fallback'
    evidence=json.loads((HERE/'tool-evidence.json').read_text(encoding='utf-8')) if (HERE/'tool-evidence.json').is_file() else {}
    counts={'commands':len(commands),'variants':sum(len(v) for v in VARIANTS.values()),'screenshots':len(evidence)}
    for ident,name,mode,icon,shortcut,predicate,failure,guide in commands:
        r=RECIPES[ident]
        for key in ('task','prepare','actions','settings','expected'):assert r[key],(ident,key)
        figures=[]
        def figure(key,title,settings,unavailable=False):
            write(HERE/'tool-diagrams'/f'{key}.svg',diagram(ident,title,settings,unavailable),check)
            return f'<figure><img src="../tool-diagrams/{key}.svg" alt="{esc(title)}の入力・操作・結果の模式図"><figcaption>模式図。実行結果の証拠画像ではありません。</figcaption></figure>'
        figures.append(figure(ident,r['task'],r['settings']))
        steps=''.join(f'<li>{esc(a)}</li>' for a in r['actions'])
        variants=[]
        for n,v in enumerate(VARIANTS.get(ident,[])):
            variants.append(f'<section id="case-{n}"><h2>{esc(v["name"])}</h2><p>準備: {esc(v["prepare"])}</p>'+figure(f'{ident}-{n}',v['name'],v['prepare'],v.get('unavailable',False))+'<ol>'+''.join(f'<li>{esc(a)}</li>' for a in v['steps'].split('→'))+f'</ol><p>確認: {esc(v["result"])}</p></section>')
        shot=''
        if ident in evidence:
            e=evidence[ident]
            shot=f'<section><h2>Windows版の操作画面</h2><figure><img src="../tool-screens/{ident}.png" alt="{esc(name)}の実画面"><figcaption>試験 {esc(e["case"])} のコマンド呼出し直後。試験全体の結果: {esc(e["case_result"])}。この時点は入力待ち・下見の場合もあり、確定後の形状を証明する画像ではありません。</figcaption></figure></section>'
        cancel='未確定入力をEscで取り消します。道具を終えるときは選択(V)または別の道具へ切り替えます。' if mode=='Tool' else 'ダイアログの取消、またはペインの取消を使います。即時操作には未確定段階がありません。'
        page=f'''<!doctype html><html lang="ja"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>{esc(name)} — 操作例</title><link rel="stylesheet" href="../manual.css"></head><body><main class="tool-page" data-tool="{ident}"><a href="../../manual.html#cmd-{ident}">全ツールへ戻る</a><h1><img class="tool-icon" src="../tool-icons/{ident}.png" alt="">{esc(name)}</h1><p class="eyebrow">具体例: {esc(r['task'])}</p>{figures[0]}<h2>準備</h2><p>{esc(r['prepare'])}</p><h2>操作手順</h2><ol data-procedure="{ident}">{steps}</ol><p class="status">設定: {esc(r['settings'])}</p><h2>結果を確認</h2><p>{esc(r['expected'])}</p><h2>取消・やり直し</h2><p>{cancel} 確定済みの文書変更はCtrl+Z、やり直しはCtrl+Yです。保存・書出し済みのファイルはUndoの対象ではありません。</p><h2>進めない場合</h2><p>{esc(failure or guide)}</p><p>対象欄と3Dの強調を照合し、必要な線・面・部品を各役割へ指定します。処理中は計算表示を確認し、取消が可能な処理は取消を使います。</p>{''.join(variants)}{shot}<footer><code>{ident}</code>。図の設定と縮尺は例です。方式ごとの制限、プレビューと確定済み結果の区別を確認してください。</footer></main></body></html>'''
        write(HERE/'tools'/f'{ident}.html',page,check)
    write(HERE/'tool-procedures-index.json',json.dumps(counts,ensure_ascii=False,indent=2)+'\n',check)
    audit_pages(commands)
    return counts

def audit_pages(commands):
    from html.parser import HTMLParser
    from urllib.parse import unquote
    import re, xml.etree.ElementTree as ET
    class Page(HTMLParser):
        def __init__(self): super().__init__();self.links=[];self.tags=[];self.images=0
        def handle_starttag(self,tag,attrs):
            a=dict(attrs)
            for key in ('href','src'):
                if key in a:self.links.append(a[key])
            if tag=='img':assert 'alt' in a;self.images+=1
            if tag not in ('meta','link','img','input','br','hr'):self.tags.append(tag)
        def handle_endtag(self,tag):assert self.tags and self.tags.pop()==tag
    for row in commands:
        ident=row[0];path=HERE/'tools'/f'{ident}.html';text=path.read_text(encoding='utf-8');page=Page();page.feed(text)
        assert not page.tags and page.images>=2,ident
        assert text.count('data-procedure=')==1,ident
        assert text.count('<section id="case-')==len(VARIANTS.get(ident,[])),ident
        for link in page.links:
            if not re.match(r'\w+://',link): assert (path.parent/unquote(link.split('#')[0])).is_file(),(ident,link)
        for svg in (HERE/'tool-diagrams').glob(ident+'*.svg'):
            root=ET.fromstring(svg.read_text(encoding='utf-8'));assert root.tag.endswith('svg')
            for image in root.findall('{http://www.w3.org/2000/svg}image'):
                assert image.attrib['href'].startswith('data:image/png;base64,'),'SVG must be self-contained'
