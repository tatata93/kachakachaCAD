"""Deterministic per-command illustrated instructions. Drawings are schematics, not test screenshots."""
from pathlib import Path
from html import escape as esc
import json, textwrap, base64
from tool_recipes import RECIPES
from tool_variants import VARIANTS
from lesson_details import DETAILS
from lesson_renderer import render_page, build_foundations
HERE=Path(__file__).resolve().parent

def write(path,text,check):
    if check:
        assert path.is_file() and path.read_text(encoding='utf-8')==text, f'Regenerate {path.name}'
    else:
        path.parent.mkdir(parents=True,exist_ok=True);path.write_text(text,encoding='utf-8',newline='\n')

def diagram(ident,title,settings,unavailable=False,prepare=None,result=None):
    """A command-specific role map, never a made-up CAD result."""
    icon_data=base64.b64encode((HERE/'tool-icons'/f'{ident}.png').read_bytes()).decode('ascii')
    r=RECIPES[ident];d=DETAILS[ident]
    def lines(value,x,y,width=22):
        return ''.join(f'<text x="{x}" y="{y+i*27}" font-size="19">{esc(t)}</text>' for i,t in enumerate(textwrap.wrap(value,width)))
    left=lines(prepare or r['prepare'],30,125,17)
    middle=lines(settings,420,190,17)
    right=lines(('未対応。代替手順へ進む' if unavailable else result or r['expected']),830,125,20)
    height=max(520,220+len(textwrap.wrap(settings,17))*27,155+len(textwrap.wrap(prepare or r['prepare'],17))*27,155+len(textwrap.wrap(result or r['expected'],20))*27)
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="1260" height="{height}" viewBox="0 0 1260 {height}"><title>{esc(title)}の操作と役割</title><rect width="1260" height="{height}" fill="#f4f8fb"/><g font-family="Yu Gothic UI,Meiryo,sans-serif" fill="#203043"><text x="30" y="42" font-size="25">{esc(title)}</text><rect x="15" y="65" width="365" height="{height-120}" rx="12" fill="white" stroke="#9bb5c6"/><rect x="405" y="65" width="365" height="{height-120}" rx="12" fill="white" stroke="#9bb5c6"/><rect x="805" y="65" width="440" height="{height-120}" rx="12" fill="white" stroke="#9bb5c6"/><text x="30" y="91" font-size="18" fill="#086992">① 用意するもの</text>{left}<image href="data:image/png;base64,{icon_data}" x="425" y="96" width="58" height="58"/><text x="500" y="125" font-size="20">② 条件・操作</text>{middle}<text x="830" y="91" font-size="18" fill="#086992">③ 結果の見分け方</text>{right}<path d="M380 255h25m-10-8 10 8-10 8M770 255h35m-10-8 10 8-10 8" fill="none" stroke="#086992" stroke-width="3"/><text x="25" y="{height-23}" font-size="17">操作の意味を整理した模式図。画面写真や実際に生成した形状・精度の証拠ではありません。</text></g></svg>'''

def build_pages(commands,check=False):
    assert set(RECIPES)==set(DETAILS)=={r[0] for r in commands},'Every command needs authored preparation, settings and recovery notes'
    build_foundations(write,check,commands)
    evidence=json.loads((HERE/'tool-evidence.json').read_text(encoding='utf-8')) if (HERE/'tool-evidence.json').is_file() else {}
    counts={'commands':len(commands),'variants':sum(len(v) for v in VARIANTS.values()),'screenshots':len(evidence)}
    for ident,name,mode,icon,shortcut,predicate,failure,guide in commands:
        r=RECIPES[ident]
        for key in ('task','prepare','actions','settings','expected'):assert r[key],(ident,key)
        figures=[]
        def figure(key,title,settings,unavailable=False,prepare=None,result=None):
            write(HERE/'tool-diagrams'/f'{key}.svg',diagram(ident,title,settings,unavailable,prepare,result),check)
            return f'<figure><img src="../tool-diagrams/{key}.svg" alt="{esc(title)}の入力・操作・結果の模式図"><figcaption>模式図。実行結果の証拠画像ではありません。</figcaption></figure>'
        figures.append(figure(ident,r['task'],r['settings']))
        variant_figures=[]
        for n,v in enumerate(VARIANTS.get(ident,[])):
            variant_figures.append(figure(f'{ident}-{n}',v['name'],v['steps'],v.get('unavailable',False),v['prepare'],v['result']))
        page=render_page((ident,name,mode,icon,shortcut,predicate,failure,guide),r,VARIANTS.get(ident,[]),evidence,variant_figures)
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
            if not re.match(r'\w+://',link):
                target,_,fragment=link.partition('#')
                destination=path.parent/unquote(target) if target else path
                assert destination.is_file(),(ident,link)
                if fragment and destination.suffix=='.html':
                    assert re.search(r'id=[\"\']'+re.escape(unquote(fragment))+r'[\"\']',destination.read_text(encoding='utf-8')),(ident,link)
        for svg in (HERE/'tool-diagrams').glob(ident+'*.svg'):
            root=ET.fromstring(svg.read_text(encoding='utf-8'));assert root.tag.endswith('svg')
            for image in root.findall('{http://www.w3.org/2000/svg}image'):
                assert image.attrib['href'].startswith('data:image/png;base64,'),'SVG must be self-contained'
    # Preparations and the promoted worked example must be packaged and navigable too.
    for path in [*(HERE/'learn').glob('*.html'),HERE/'headlight-to-plane.html']:
        page=Page()
        try:page.feed(path.read_text(encoding='utf-8'))
        except AssertionError as error:raise AssertionError(('Invalid HTML',path.name)) from error
        assert not page.tags,('Unclosed HTML',path)
        for link in page.links:
            if re.match(r'\w+://',link):continue
            target,_,fragment=link.partition('#')
            destination=path.parent/unquote(target) if target else path
            assert destination.is_file(),(path.name,link)
            if fragment and destination.suffix=='.html':
                assert re.search(r'id=[\"\']'+re.escape(unquote(fragment))+r'[\"\']',destination.read_text(encoding='utf-8')),(path.name,link)
