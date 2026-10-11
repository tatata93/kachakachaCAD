"""Independent STL distance check against the explicitly estimated nose sections.

This measures CAD/export fidelity to the reference study, not factory accuracy.
The manufacturer's envelope is independently reported beside the study bounds.
"""
import argparse,json
from pathlib import Path
import numpy as np
import trimesh
from practical_n700_fixture import section

p=argparse.ArgumentParser()
p.add_argument('--nose',required=True);p.add_argument('--notes',required=True)
p.add_argument('--output',required=True);a=p.parse_args()
notes=json.loads(Path(a.notes).read_text())
mesh=trimesh.load_mesh(a.nose,process=False);mesh.merge_vertices()
points=[];station_ids=[]
for i,station in enumerate(notes['sections_mm']):
    for curve in section(*station):
        controls=np.asarray(curve)
        for t in np.linspace(0,1,33):
            points.append(np.array([(1-t)**3,3*t*(1-t)**2,3*t*t*(1-t),t**3])@controls)
            station_ids.append(i)
points=np.asarray(points);station_ids=np.asarray(station_ids)
nearest,distance,triangles=trimesh.proximity.closest_point(mesh,points)
sections=[]
for i,station in enumerate(notes['sections_mm']):
    d=distance[station_ids==i]
    sections.append(dict(x_mm=station[0],samples=len(d),maximum_distance_mm=float(d.max()),rms_mm=float(np.sqrt(np.mean(d*d)))))
# Dense plane intersections expose bulging, missing slices, and gross twisting
# between the supplied nose stations. Record the full measured envelope.
slices=[]
for x in np.linspace(.05,63.95,129):
    lines=trimesh.intersections.mesh_plane(mesh,[1,0,0],[x,0,0])
    if not len(lines):slices.append(dict(x_mm=float(x),missing=True));continue
    yz=lines.reshape(-1,3)[:,1:]
    slices.append(dict(x_mm=float(x),y_min=float(yz[:,0].min()),y_max=float(yz[:,0].max()),
        z_min=float(yz[:,1].min()),z_max=float(yz[:,1].max()),
        left_right_extent_error_mm=float(abs(yz[:,0].min()+yz[:,0].max()))))
report=dict(source=notes['reference'],reference_uncertainty='Photo estimated sections; manufacturer supplies envelope only.',
    nose_file=str(Path(a.nose).resolve()),samples=len(points),maximum_distance_mm=float(distance.max()),
    rms_distance_mm=float(np.sqrt(np.mean(distance**2))),reference_sections=sections,dense_sections=slices,
    bounds_mm=mesh.bounds.tolist(),watertight=bool(mesh.is_watertight),winding_consistent=bool(mesh.is_winding_consistent),
    manufacturer_body_envelope_mm=[27100/150,3360/150,3600/150],
    export_section_tolerance_mm=.015,export_section_pass=bool(distance.max()<=.015),
    missing_slices=sum(s.get('missing',False) for s in slices))
report['width_excess_over_reference_mm']=float(max(0,mesh.extents[1]-notes['width_mm']))
report['nose_envelope_pass']=bool(report['width_excess_over_reference_mm']<=.03
    and mesh.bounds[1,2]<=notes['height_mm']+.015)
roof=np.array([s['z_max'] for s in slices if not s.get('missing')])
report['maximum_unintended_roof_dip_mm']=float(np.max(np.maximum.accumulate(roof)-roof))
report['roof_monotonic_within_mesh_tolerance']=bool(report['maximum_unintended_roof_dip_mm']<=.02)
Path(a.output).write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if k not in ('dense_sections','reference_sections')},indent=2))
