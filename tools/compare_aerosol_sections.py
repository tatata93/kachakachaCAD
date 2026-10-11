"""Read-only radial sections against the exact, photo-estimated CAD profile.
Agreement with this profile is not measurement accuracy against a real can.
"""
import argparse,json,math
from pathlib import Path
import numpy as np
import trimesh

p=argparse.ArgumentParser();p.add_argument('files',nargs='+');p.add_argument('--output',required=True);a=p.parse_args()
angles=np.deg2rad(np.linspace(1,89,89));heights=np.linspace(2.6,70.9,138)
zz,aa=np.meshgrid(heights,angles,indexing='ij');zz=zz.ravel();aa=aa.ravel()
directions=np.column_stack((np.cos(aa),np.sin(aa),np.zeros(len(aa))))
origins=np.column_stack((np.zeros(len(aa)),np.zeros(len(aa)),zz))
def radius(z):
    if z<=62:return 16.2
    lo,hi=0.,1.
    for _ in range(45):
        t=(lo+hi)*.5;s=1-t
        v=s**3*62+3*s*s*t*66+3*s*t*t*68+t**3*71
        if v<z:lo=t
        else:hi=t
    return s**3*16.2+3*s*s*t*16.2+3*s*t*t*10.6+t**3*10.6
expected=np.array([radius(z) for z in zz]);reports=[];section_rows=[]
for filename in a.files:
    mesh=trimesh.load_mesh(filename,process=True)
    points,rays,_=mesh.ray.intersects_location(origins,directions,multiple_hits=True)
    rr=np.linalg.norm(points-origins[rays],axis=1);rows=[];bad=[]
    for i in range(len(zz)):
        hits=np.sort(rr[rays==i]);hits=hits[np.r_[True,np.diff(hits)>1e-5]] if len(hits) else hits
        if len(hits)!=2:bad.append(dict(z=float(zz[i]),angle=float(np.rad2deg(aa[i])),hits=hits.tolist()))
        else:rows.append([i,*hits])
    rows=np.asarray(rows);ids=rows[:,0].astype(int);outer=rows[:,2]-expected[ids]
    section_rows.append({int(row[0]):row[1:].tolist() for row in rows})
    reports.append(dict(file=filename,rays=len(zz),invalid_intervals=bad,
        outer_error_mm=dict(max_abs=float(abs(outer).max()),rms=float(np.sqrt(np.mean(outer**2)))),
        radial_thickness_mm=[float((rows[:,2]-rows[:,1]).min()),float((rows[:,2]-rows[:,1]).max())],
        note='Radial thickness increases on sloped shoulder; this is not normal wall thickness. Profile stations are photo estimates.'))
if len(section_rows)==2:
    common=sorted(section_rows[0].keys()&section_rows[1].keys())
    delta=np.array([np.array(section_rows[1][i])-section_rows[0][i] for i in common])
    comparison=dict(paired_rays=len(common),inner_outer_max_abs_mm=np.max(abs(delta),axis=0).tolist(),
        inner_outer_rms_mm=np.sqrt(np.mean(delta**2,axis=0)).tolist())
else:comparison=None
result=dict(reference='exact line + cubic Bezier main-shell profile; r16.2 at z2.5..62 then shoulder to r10.6,z71',
    reports=reports,comparison=comparison)
Path(a.output).write_text(json.dumps(result,indent=2),encoding='utf-8')
print(json.dumps(result,indent=2))
