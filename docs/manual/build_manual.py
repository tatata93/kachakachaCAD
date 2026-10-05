"""Generate the offline manual and audit command/asset coverage. Run from any cwd."""
from pathlib import Path
import re, json, html, argparse
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
Q = r'"((?:[^"\\]|\\.)*)"'
PAT = r'\{\s*'+Q+r'\s*,\s*'+Q+r'\s*,\s*CommandMode::(\w+)\s*,\s*'+Q+r'\s*,\s*'+Q+r'\s*,\s*SelectionPredicate::(\w+)\s*,\s*'+Q+r'\s*,\s*'+Q
GROUPS = {
 'draw': ('作図・曲線', 'draw', 'v2-curves.png'),
 'wire': ('ワイヤーの編集・変形・投影', 'wire-edit', 'v2-curves.png'),
 'workplane': ('作業平面・フェイスで作図', 'workplane', 'v2-grid.png'),
 'grid': ('グリッド', 'draw', 'v2-grid.png'),
 'surface': ('面の作成・編集', 'guide', 'v2-ui-surface-preview.png'),
 'guide': ('面の役割表・回転面', 'guide', 'v2-guide-table.png'),
 'part': ('立体・加工・配置', 'part', 'v2-ui-tool-fillet.png'),
 'fabrication': ('製作近似・曲げ・型紙・生成', 'fabrication', 'v2-ui-approx-candidates.png'),
 'instructions': ('組み立て説明書', 'assembly-guide', 'v2-instructions-3d.png'),
 'image': ('画像の貼付・編集', 'image-placement', 'image-fit.svg'),
 'export': ('選択出力・型紙出力', 'selection-output', 'v2-export.png'),
 'output': ('同一文書への配置', 'selection-output', 'v2-export.png'),
 'group': ('グループ・子グループ', 'groups-guide', 'v2-ui-explorer-groups.png'),
 'measure': ('測定', 'measure', 'v2-ui-measure-overlay.png'),
 'common': ('保存・選択・表示・共通操作', 'screen', 'v2-tools.png'),
}

def figure(name, caption):
    src='manual/'+('diagrams/' if name.endswith('.svg') else 'images/')+name
    return f'<figure><a href="{src}" target="_blank" rel="noopener"><img src="{src}" alt="{html.escape(caption)}" loading="lazy"></a><figcaption>{html.escape(caption)}。画像をクリックすると原寸表示。</figcaption></figure>'

def commands():
    text=(ROOT/'src/next/kachakacha/app/CommandCatalog.cpp').read_text(encoding='utf-8')
    rows=re.findall(PAT,text)
    ids=re.findall(r'\{\s*"([a-z_]+\.[a-z_0-9]+)"\s*,\s*"',text)
    assert len(rows)==len(ids)==len(set(ids)), 'Command parser must cover every entry'
    return rows

def build():
    source=(HERE/'chapters.html').read_text(encoding='utf-8')
    extra=(HERE/'illustrated-workflows.html').read_text(encoding='utf-8')
    notes=json.loads((HERE/'command-notes.json').read_text(encoding='utf-8'))
    rows=commands(); sections=re.findall(r'<section\b[^>]*>.*?</section>',source+'\n'+extra,re.S)
    # Keep tutorials next to the associated feature instead of appending updates after the index.
    order=['about','quick-workflow','start','first-sheet','screen','tool-search-focus',
           'groups-guide','draw','workplane','wire-edit','measure','area-picture',
           'image-placement','image-fit-guide','guide','loft-picture','part','solid-recipes',
           'contact-operations','boolean-picture','fabrication','adaptive-guide',
           'export','selection-output','assembly-guide','railway','limits','shortcuts']
    sections.sort(key=lambda s: order.index(re.search(r'id="([^"]+)"',s).group(1))
                  if re.search(r'id="([^"]+)"',s).group(1) in order else len(order))
    refs=[]
    for key,(label,chapter,picture) in GROUPS.items():
        matching=[r for r in rows if (r[0].split('.')[0] if r[0].split('.')[0] in GROUPS else 'common')==key]
        if not matching: continue
        cards=[]
        for ident,name,mode,icon,shortcut,predicate,failure,guide in matching:
            escape=lambda s:html.escape(s.replace('\\n',' ').replace('\\"','"'))
            entry={'Tool':'道具を選び、画面の案内に従って対象・点を指定します。右ペインの確定操作を使います。',
                   'Instant':'選択が必要な操作は対象を指定して実行します。実行時の選択・状態で処理します。',
                   'Dialog':'設定画面を開き、対象・条件または保存先を確認して実行します。',
                   'Modeless':'操作ペインを開き、3Dビューの選択と設定を行います。'}[mode]
            detail=notes.get(ident,'')
            if detail and html.unescape(re.sub('<[^>]+>','',detail))==guide: detail=''
            cards.append(f'<article class="command" id="cmd-{ident}" data-command="{ident}"><h3>{escape(name)}'+(f' <kbd>{escape(shortcut)}</kbd>' if shortcut else '')+f'</h3><p>{escape(guide)}</p>'+(f'<p>{detail}</p>' if detail else '')+f'<p class="meta">操作: {entry}</p>'+(f'<p class="meta">対象・実行条件の案内: {escape(failure)}</p>' if failure else '')+f'<p><a href="#{chapter}">図と操作手順を見る</a> · <code>{ident}</code></p></article>')
        refs.append(f'<section id="ref-{key}"><h2>{label} — 全コマンド一覧</h2>'+figure(picture,label+'の参考画面・模式図（個々の設定は下の項目を参照）')+'\n'.join(cards)+'</section>')
    sections+=refs
    nav=[]
    for s in sections:
        ident=re.search(r'<section[^>]*id="([^"]+)"',s).group(1)
        title=re.search(r'<h[12]>(.*?)</h[12]>',s,re.S).group(1)
        nav.append(f'<li><a href="#{ident}">{title}</a></li>')
    text='''<!doctype html>
<html lang="ja"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="kachakacha-manual-version" content="v2"><title>kachakachaCAD V2 画像付きユーザーマニュアル</title>
<link rel="stylesheet" href="manual/manual.css"><script src="manual/manual.js" defer></script></head>
<body><a class="skip" href="#manual-content">本文へ</a><div class="layout"><nav aria-label="目次"><strong>kachakachaCAD</strong><span>画像付きユーザーマニュアル</span>
<label for="manual-search">全機能を検索</label><input id="manual-search" type="search" placeholder="例: 円弧 / 画像 / 押し出し"><div class="toolbar"><button id="clear-search">検索解除</button><button id="print-manual">印刷 / PDF</button></div><p id="search-result" role="status"></p><ol>'''+''.join(nav)+'''</ol></nav><main id="manual-content">
<p class="eyebrow">V2 / 2026-10-05 / オフライン対応</p>
'''+f'<p>作図・部品・製作・出力・説明書の5モード。現在の登録コマンド <strong>{len(rows)}件</strong> を掲載しています。図はWindows版の実画面と、操作を説明する模式図です。模式図は実画面ではありません。機能名・用途で検索し、図をクリックして拡大できます。</p>\n'+'\n'.join(sections)+'''
<footer>対象: codex/v2-wp01-build-scaffold、ソース d767ab58d 時点。記載の制限は解消済みとは扱いません。表示形状・製作近似の誤差と実製作寸法を確認してください。HTMLと隣のmanualフォルダーを一緒に保管すればインターネットなしで読めます。</footer></main></div></body></html>
'''
    # Every illustration is expandable, including inherited tutorial figures.
    text=re.sub(r'(<figure[^>]*>)\s*(<img\b[^>]*src="([^"]+)"[^>]*>)',
                r'\1<a href="\3" target="_blank" rel="noopener">\2</a>',text)
    text=re.sub(r'<img(?![^>]*loading=)', '<img loading="lazy"', text)
    return text

def audit(text):
    from html.parser import HTMLParser
    class Audit(HTMLParser):
        def __init__(self): super().__init__(); self.ids=[];self.links=[];self.images=[];self.tags=[]
        def handle_starttag(self,tag,attrs):
            a=dict(attrs)
            if 'id' in a:self.ids.append(a['id'])
            for key in ('href','src'):
                if key in a:self.links.append(a[key])
            if tag=='img': assert a.get('alt');self.images.append(a['src'])
            if tag not in ('meta','link','img','input','br','hr','wbr'):self.tags.append(tag)
        def handle_endtag(self,tag):
            assert self.tags and self.tags.pop()==tag, f'Unbalanced HTML: {tag}'
    p=Audit();p.feed(text);assert not p.tags
    assert len(p.ids)==len(set(p.ids)), 'Duplicate anchors'
    for link in p.links:
        if link.startswith('#'):assert link[1:] in p.ids,link
        elif not re.match(r'\w+://',link):assert (ROOT/'docs'/unquote(link.split('#')[0])).exists(),link
    for row in commands():assert text.count('data-command="'+row[0]+'"')==1,row[0]
    assert len(re.findall('data-command=',text))==len(commands())
    assert len(text.splitlines())<=1500
    print(f'PASS: {len(commands())} commands; {len(set(p.images))} illustrations; anchors, local files, balanced HTML and line limit')

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--check',action='store_true');args=parser.parse_args()
    output=build();audit(output)
    target=ROOT/'docs/manual.html'
    if args.check:assert target.read_text(encoding='utf-8')==output,'Run build_manual.py to regenerate'
    else:target.write_text(output,encoding='utf-8',newline='\n')
