"""Analytic radial cross-section checks for the r16/H70/t0.8 control specimen."""
import argparse,json,math
from pathlib import Path
import numpy as np
import trimesh
p=argparse.ArgumentParser();p.add_argument('files',nargs='+');p.add_argument('--output',required=True);a=p.parse_args()
reports=[]
for file in a.files:
    mesh=trimesh.load_mesh(file,process=True)
    angles=np.linspace(.25,89.75,359)*math.pi/180
    directions=np.column_stack((np.cos(angles),np.sin(angles),np.zeros(len(angles))))
    origins=np.tile((0,0,35),(len(angles),1))
    points,rays,_=mesh.ray.intersects_location(origins,directions,multiple_hits=True)
    radii=np.linalg.norm(points-origins[rays],axis=1)
    rows=[];bad=[]
    for i in range(len(angles)):
        hits=sorted(radii[rays==i]);unique=[]
        for r in hits:
            if not unique or abs(r-unique[-1])>1e-5:unique.append(float(r))
        if len(unique)!=2:bad.append(dict(angle_degrees=float(angles[i]*180/math.pi),hits=unique))
        else:rows.append(unique)
    rows=np.asarray(rows);outer=rows[:,1]-16;inner=rows[:,0]-15.2
    report=dict(file=file,reference='analytic quarter cylindrical shell r16, inner r15.2, H70 mm',
        ray_count=len(angles),invalid_ray_intervals=bad,volume_mm3=float(mesh.volume),
        analytic_volume_mm3=math.pi/4*(16**2-15.2**2)*70,
        outer_deviation_mm=dict(max_abs=float(abs(outer).max()),rms=float(np.sqrt(np.mean(outer**2)))),
        inner_deviation_mm=dict(max_abs=float(abs(inner).max()),rms=float(np.sqrt(np.mean(inner**2)))),
        radial_wall_range_mm=[float((rows[:,1]-rows[:,0]).min()),float((rows[:,1]-rows[:,0]).max())],
        caveat='Ray sampling is at mid-height and excludes end cuts; this does not certify all self-intersections or global minimum wall thickness.')
    reports.append(report)
Path(a.output).write_text(json.dumps(reports,indent=2),encoding='utf-8');print(json.dumps(reports,indent=2))
