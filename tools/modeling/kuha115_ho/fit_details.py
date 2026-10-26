"""Printable mating seats. Dimensions describe model construction, not measured fittings."""
REVISION='2026-10-26-seated-glazing-skirt-cooler'

def contact(m,first,second,label,area=.2,region=None):
    if not hasattr(m,'attachment_checks'):m.attachment_checks=[]
    row=dict(first=first,second=second,label=label,maximumDistanceMm=1e-5,minimumAreaMm2=area)
    if region is not None:row['region']=region
    m.attachment_checks.append(row)

def replace_checks(m,old,new):
    for c in m.checks:
        for key in ['first','second']:
            if c[key]==old:c[key]=new
    for c in m.points:
        if c['entity']==old:c['entity']=new
    for c in getattr(m,'attachment_checks',[]):
        for key in ['first','second']:
            if c[key]==old:c[key]=new

def panorama_seat(m,g,side):
    # 0.43 mm deep curved ledge, fused into the inner wall. Its front face
    # supports the existing 0.4 mm glazing at inset 0.62, without pushing it out.
    shell=m.shell('パノラミック窓の内側接着受け',g,.62,1.05,30.5,38.3)
    outer=m.rounded('窓受け外周',(-3,5.2 if side>0 else -18.5,30.5),(0,1,0),(0,0,1),13.3,7.8,.9,g)
    clip=m.extrude('窓受け外周工具',[outer],(1,0,0),11.55,g)
    seat=m.boolean('回込窓の曲面受け',shell,[clip],2,g)
    inner=m.rounded('窓受け内側の透視開口',(-3,5.5 if side>0 else -18.2,30.8),(0,1,0),(0,0,1),12.7,7.2,.6,g)
    hole=m.extrude('窓受けの内側工具',[inner],(1,0,0),11.15,g)
    seat=m.boolean('窓の視界を残す接着受け',seat,[hole],1,g)
    previous=m.finished['body'];body=m.joined('車体・回込窓の接着受け一体',[previous,seat],m.entities[previous]['groupId'])
    replace_checks(m,previous,body);m.finished['body']=body
