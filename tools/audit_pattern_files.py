"""Independent reread of line-based practical SVG/DXF/PDF pattern exports.

Checks numerical output and XML validity; never rewrites the source files.
PDF rendering is a separate Poppler visual check. Curved path commands and
compressed PDF streams are deliberately unsupported, never silently accepted.
"""
import argparse, collections, hashlib, json, math, re
from pathlib import Path
import xml.etree.ElementTree as ET

number=r'[-+]?(?:\d*\.\d+|\d+)(?:[eE][-+]?\d+)?'
def audit(stem):
    files={suffix:Path(str(stem)+suffix) for suffix in ('.svg','.dxf','.pdf')}
    page_match=re.fullmatch(r'(.*)-page-(\d+)',str(stem))
    page_index=int(page_match[2])-1 if page_match else 0
    if page_match:files['.pdf']=Path(page_match[1]+'.pdf')
    report={'stem':str(stem),'sha256':{k:hashlib.sha256(v.read_bytes()).hexdigest() for k,v in files.items()}}
    svg=files['.svg'].read_text(encoding='utf-8')
    try:ET.fromstring(svg);report['svg_xml_valid']=True
    except ET.ParseError as e:report.update(svg_xml_valid=False,xml_error=str(e))
    height=float(re.search(r'height="('+number+r')mm"',svg)[1])
    svg_lines=collections.defaultdict(list)
    for layer,group in re.findall(r'<g id="([^"]+)"[^>]*>(.*?)</g>',svg,re.S):
        for path in re.findall(r'<path\s+d="([^"]+)"',group):
            match=re.fullmatch(r'M ('+number+'),('+number+') L ('+number+'),('+number+')',path)
            if not match:raise ValueError('Non-line SVG needs a curve-aware comparison')
            x,y,u,v=map(float,match.groups());svg_lines[layer].append((x,height-y,u,height-v))
    text=files['.dxf'].read_text(encoding='utf-8').splitlines()
    pairs=list(zip(text[::2],text[1::2]));entities=[];current=None
    for code,value in pairs:
        code=code.strip();value=value.strip()
        if code=='0':
            if current:entities.append(current)
            current={'type':value,'values':{}}
        elif current:current['values'].setdefault(code,[]).append(value)
    if current:entities.append(current)
    dxf_lines=collections.defaultdict(list);ordered=[]
    for e in entities:
        if e['type'] in ('SECTION','ENDSEC','EOF'):continue
        if e['type']!='LINE':raise ValueError('Non-line DXF needs a curve-aware comparison')
        v=e['values'];line=tuple(float(v[c][0]) for c in ('10','20','11','21'))
        dxf_lines[v['8'][0]].append(line);ordered.append(line)
    counts_equal={k:len(v) for k,v in svg_lines.items()}=={k:len(v) for k,v in dxf_lines.items()}
    difference=max((abs(a-b) for layer,lines in svg_lines.items() for x,y in zip(lines,dxf_lines[layer]) for a,b in zip(x,y)),default=math.inf)
    pdf=files['.pdf'].read_bytes();streams=re.findall(rb'stream\r?\n(.*?)\r?\nendstream',pdf,re.S)
    if b'/FlateDecode' in pdf:raise ValueError('Compressed PDF stream requires a PDF parser')
    page_count=len(re.findall(rb'/Type\s*/Page\b',pdf))
    content=streams[page_index].decode('ascii')
    pattern=r'('+number+r') ('+number+r') m\s+('+number+r') ('+number+r') l\s+S'
    pdf_lines=[tuple(float(x)*25.4/72 for x in values) for values in re.findall(pattern,content)]
    # PDF groups paths by layer; DXF preserves insertion order. Compare the
    # same layer order, without sorting coordinates or tolerating missing lines.
    pdf_order=[line for layer in ('OUTLINE','OPENING','RELIEF','FOLD','ANNOTATION') for line in dxf_lines[layer]]
    pdf_difference=max((abs(a-b) for x,y in zip(pdf_order,pdf_lines) for a,b in zip(x,y)),default=math.inf)
    report.update(layer_line_counts={k:len(v) for k,v in svg_lines.items()},svg_dxf_same_counts=counts_equal,
        svg_dxf_max_coordinate_difference_mm=difference,pdf_page_count=page_count,pdf_page_number=page_index+1,pdf_page_line_count=len(pdf_lines),
        pdf_dxf_max_coordinate_difference_mm=pdf_difference,
        scope='Line endpoints in millimetres; corresponding PDF page matches single-sheet SVG/DXF. Separate visual QA required.')
    report['pass']=report['svg_xml_valid'] and counts_equal and difference<=1e-4 and len(pdf_lines)==len(ordered) and pdf_difference<=.0003
    return report

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('directory');p.add_argument('--output',required=True);a=p.parse_args()
    reports=[]
    for svg in sorted(Path(a.directory).glob('pattern*.svg')):
        try:reports.append(audit(svg.with_suffix('')))
        except Exception as e:reports.append({'stem':str(svg.with_suffix('')),'pass':False,'error':str(e)})
    for report in reports:
        if report.get('pdf_page_number')!=1:continue
        missing=[page for page in range(2,report['pdf_page_count']+1)
            if not all(Path(report['stem']+f'-page-{page:03}'+suffix).exists() for suffix in ('.svg','.dxf'))]
        report['missing_svg_dxf_pages']=missing
        report['pass']=report['pass'] and not missing
    Path(a.output).write_text(json.dumps(reports,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps([{'stem':r['stem'],'pass':r['pass'],'error':r.get('error',r.get('xml_error'))} for r in reports],ensure_ascii=False))
    raise SystemExit(0 if reports and all(r['pass'] for r in reports) else 1)
