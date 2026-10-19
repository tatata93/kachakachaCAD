"""Deterministic per-command lessons using recorded application screens."""
import os
import tempfile
from pathlib import Path
from html import escape as esc
import json
from tool_recipes import RECIPES
from tool_variants import VARIANTS
from lesson_details import DETAILS
from lesson_renderer import render_page, build_foundations, variant_figure
HERE=Path(__file__).resolve().parent

def write(path,text,check):
    if check:
        assert path.is_file() and path.read_text(encoding='utf-8')==text, f'Regenerate {path.name}'
    else:
        path.parent.mkdir(parents=True,exist_ok=True)
        with tempfile.NamedTemporaryFile(mode='w',encoding='utf-8',newline='\n',dir=path.parent,prefix=path.name+'.',suffix='.tmp',delete=False) as out:
            temporary=Path(out.name)
            out.write(text)
        try:os.replace(temporary,path)
        finally:temporary.unlink(missing_ok=True)

def build_pages(commands,check=False):
    assert set(RECIPES)==set(DETAILS)=={r[0] for r in commands},'Every command needs authored preparation, settings and recovery notes'
    build_foundations(write,check,commands)
    evidence=json.loads((HERE/'tool-evidence.json').read_text(encoding='utf-8')) if (HERE/'tool-evidence.json').is_file() else {}
    counts={'commands':len(commands),'variants':sum(len(v) for v in VARIANTS.values()),'screenshots':len(evidence),'schematics':0}
    for ident,name,mode,icon,shortcut,predicate,failure,guide in commands:
        r=RECIPES[ident]
        for key in ('task','prepare','actions','settings','expected'):assert r[key],(ident,key)
        variant_figures=[variant_figure(ident,n,v['name'],v.get('unavailable',False))
                        for n,v in enumerate(VARIANTS.get(ident,[]))]
        page=render_page((ident,name,mode,icon,shortcut,predicate,failure,guide),r,VARIANTS.get(ident,[]),evidence,variant_figures)
        write(HERE/'tools'/f'{ident}.html',page,check)
    write(HERE/'tool-procedures-index.json',json.dumps(counts,ensure_ascii=False,indent=2)+'\n',check)
    audit_pages(commands)
    return counts

def audit_pages(commands):
    from html.parser import HTMLParser
    from urllib.parse import unquote
    import re
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
        assert '<svg' not in text and not any(link.partition('#')[0].endswith('.svg') for link in page.links), ident
        screenshot=HERE/'tool-screens'/f'{ident}.png'
        assert screenshot.read_bytes().startswith(b'\x89PNG\r\n\x1a\n'), ('Not a recorded PNG', ident)
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
