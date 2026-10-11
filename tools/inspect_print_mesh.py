"""Independent exported-STL checks; never repairs or rewrites the input.

Uses optional numpy/trimesh/rtree. Ray thickness is a sampled diagnostic, not a
certified global minimum. Closed/manifold does not imply no self-intersection.
"""
import argparse, hashlib, json
from pathlib import Path
import numpy as np
import trimesh

def inspect(path, nominal=0.8):
    raw=trimesh.load_mesh(path,process=False)
    if isinstance(raw,trimesh.Scene): raw=raw.to_mesh()
    # STL repeats vertex coordinates per triangle. Weld only in the analysis copy.
    mesh=raw.copy(); mesh.merge_vertices(digits_vertex=5)
    counts=np.bincount(mesh.edges_unique_inverse)
    duplicate_count=len(mesh.faces)-len(np.unique(np.sort(mesh.faces,axis=1),axis=0))
    areas=mesh.area_faces
    shells=mesh.split(only_watertight=False)
    # A closed cavity has inward-oriented faces and negative signed shell volume.
    # Check alternating orientation by nesting, rather than rejecting every cavity.
    shell_orientation=[]
    for i,shell in enumerate(shells):
        depth=0;ambiguous=False
        sample=shell.vertices[np.linspace(0,len(shell.vertices)-1,min(64,len(shell.vertices)),dtype=int)]
        for j,other in enumerate(shells):
            if i==j or not other.is_watertight:continue
            if np.any(shell.bounds[1]<other.bounds[0]) or np.any(shell.bounds[0]>other.bounds[1]):continue
            inside=other.contains(sample)
            if inside.all():depth+=1
            elif inside.any():ambiguous=True
        expected=1 if depth%2==0 else -1
        shell_orientation.append(dict(nesting_depth=depth,expected_sign=expected,ambiguous_overlap=ambiguous,
            orientation_pass=bool(shell.is_watertight and not ambiguous and shell.volume*expected>0)))
    report=dict(file=str(path.resolve()),sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
        units='millimetres by export contract; STL carries no unit metadata',
        vertices=len(mesh.vertices),triangles=len(mesh.faces),bounds_mm=mesh.bounds.tolist(),
        dimensions_mm=mesh.extents.tolist(),area_mm2=float(mesh.area),volume_mm3=float(mesh.volume),
        watertight=bool(mesh.is_watertight),consistent_winding=bool(mesh.is_winding_consistent),
        boundary_edges=int(np.sum(counts==1)),nonmanifold_edges=int(np.sum(counts>2)),
        duplicate_triangles=int(duplicate_count),near_zero_area_triangles=int(np.sum(areas<1e-10)),
        finite_coordinates=bool(np.isfinite(mesh.vertices).all()),
        connected_shells=len(shells),shells=[dict(triangles=len(s.faces),volume_mm3=float(s.volume),
            watertight=bool(s.is_watertight),bounds_mm=s.bounds.tolist(),**shell_orientation[i]) for i,s in enumerate(shells)],
        shell_orientation_method='64 sampled vertices per shell containment; nesting alternates material/cavity. Not a self-intersection certificate.',
        self_intersection='not certified by this topology/ray check')
    ids=np.linspace(0,len(mesh.faces)-1,min(2000,len(mesh.faces)),dtype=int)
    normals=mesh.face_normals[ids]
    centers=mesh.triangles_center[ids]
    eps=1e-5
    # Bound the temporary ray/triangle candidate arrays for high-detail exports.
    hit_locations=[];hit_rays=[]
    for first in range(0,len(centers),32):
        locations,rays,_=mesh.ray.intersects_location(centers[first:first+32]-normals[first:first+32]*eps,-normals[first:first+32],multiple_hits=False)
        hit_locations.append(locations);hit_rays.append(rays+first)
    locations=np.concatenate(hit_locations);rays=np.concatenate(hit_rays)
    distances=np.linalg.norm(locations-centers[rays],axis=1)
    report['normal_ray_wall_probe']=dict(samples=len(ids),hits=len(distances),
        nominal_mm=nominal,quantiles_mm=np.quantile(distances,[0,.01,.05,.5,.95,1]).tolist() if len(distances) else [],
        below_80_percent_nominal=int(np.sum(distances<nominal*.8)),
        caveat='Interior normal rays can hit a neighbouring wall; sharp corners and tessellation affect results. Not a certified minimum wall thickness.')
    report['topology_pass']=bool(report['watertight'] and report['consistent_winding'] and
        report['finite_coordinates'] and not report['duplicate_triangles'] and
        not report['near_zero_area_triangles'] and report['volume_mm3']>0 and all(s['orientation_pass'] for s in report['shells']))
    return report

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('files',nargs='+');p.add_argument('--output',required=True)
    p.add_argument('--nominal',type=float,default=.8);a=p.parse_args()
    reports=[]
    for name in a.files:
        try:reports.append(inspect(Path(name),a.nominal))
        except Exception as e:reports.append(dict(file=name,error=str(e),topology_pass=False))
    Path(a.output).write_text(json.dumps(reports,indent=2),encoding='utf-8')
    print(json.dumps([{'file':r['file'],'topology_pass':r['topology_pass'],'error':r.get('error')} for r in reports],indent=2))
