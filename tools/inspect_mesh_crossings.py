"""Independent triangle intersection audit; reads meshes without repairing them.

Reports proper noncoplanar penetration and positive-area coplanar overlap.
Touching only at a point/edge is not a proper intersection; topology is audited
separately. Floating point resolution and the specified tolerance remain limits.
"""
import argparse,json,time
from pathlib import Path
import numpy as np
import trimesh

def cross2(a,b):return a[0]*b[1]-a[1]*b[0]
def coplanar_area(a,b,normal,tol):
    origin=a[0].copy();a=a-origin;b=b-origin
    axis=int(np.argmax(np.abs(normal)));a=np.delete(a,axis,axis=1);b=np.delete(b,axis,axis=1)
    orientation=1 if cross2(b[1]-b[0],b[2]-b[0])>0 else -1
    polygon=list(a)
    for k in range(3):
        edge=b[(k+1)%3]-b[k];next_poly=[]
        for s,e in zip(polygon,polygon[1:]+polygon[:1]):
            ds=orientation*cross2(edge,s-b[k]);de=orientation*cross2(edge,e-b[k])
            if (ds>=0)!=(de>=0):next_poly.append(s+(e-s)*ds/(ds-de))
            if de>=0:next_poly.append(e)
        polygon=next_poly
        if len(polygon)<3:return 0.
    return abs(sum(cross2(a,b) for a,b in zip(polygon,polygon[1:]+polygon[:1])))*.5/max(abs(normal[axis]),tol)

def penetrating_edges(a,b,normal,tol):
    d=np.einsum('nij,nj->ni',a-b[:,0,None,:],normal)
    u=b[:,1]-b[:,0];v=b[:,2]-b[:,0]
    uu=np.einsum('ij,ij->i',u,u);uv=np.einsum('ij,ij->i',u,v);vv=np.einsum('ij,ij->i',v,v)
    denominator=uu*vv-uv*uv;hit=np.zeros(len(a),dtype=bool)
    for j in range(3):
        k=(j+1)%3;crossing=((d[:,j]>tol)&(d[:,k]<-tol))|((d[:,k]>tol)&(d[:,j]<-tol))
        ids=np.flatnonzero(crossing&(denominator>1e-24))
        if not len(ids):continue
        t=d[ids,j]/(d[ids,j]-d[ids,k]);q=a[ids,j]+t[:,None]*(a[ids,k]-a[ids,j])-b[ids,0]
        qu=np.einsum('ij,ij->i',q,u[ids]);qv=np.einsum('ij,ij->i',q,v[ids])
        x=(qu*vv[ids]-qv*uv[ids])/denominator[ids];y=(qv*uu[ids]-qu*uv[ids])/denominator[ids]
        hit[ids]|=(x>1e-9)&(y>1e-9)&(x+y<1-1e-9)
    return hit

def inspect_pairs(triangles,pairs,tol):
    pair=np.asarray(pairs,dtype=np.int64);a=triangles[pair[:,0]];b=triangles[pair[:,1]]
    na=np.cross(a[:,1]-a[:,0],a[:,2]-a[:,0]);nb=np.cross(b[:,1]-b[:,0],b[:,2]-b[:,0])
    na/=np.maximum(np.linalg.norm(na,axis=1)[:,None],1e-30);nb/=np.maximum(np.linalg.norm(nb,axis=1)[:,None],1e-30)
    da=np.einsum('nij,nj->ni',a-b[:,0,None,:],nb);db=np.einsum('nij,nj->ni',b-a[:,0,None,:],na)
    coplanar=(np.max(np.abs(da),axis=1)<=tol)&(np.max(np.abs(db),axis=1)<=tol)
    possible=(np.min(da,axis=1)<-tol)&(np.max(da,axis=1)>tol)&(np.min(db,axis=1)<-tol)&(np.max(db,axis=1)>tol)
    ids=np.flatnonzero(possible);hits=[]
    if len(ids):
        penetration=penetrating_edges(a[ids],b[ids],nb[ids],tol)|penetrating_edges(b[ids],a[ids],na[ids],tol)
        hits.extend({'faces':pair[k].tolist(),'kind':'proper_penetration'} for k in ids[penetration])
    for k in np.flatnonzero(coplanar):
        # Coplanar adjacent triangles are allowed to share only a zero-area edge.
        area=coplanar_area(a[k],b[k],na[k],tol)
        span=max(np.linalg.norm(a[k]-np.roll(a[k],1,axis=0),axis=1).max(),np.linalg.norm(b[k]-np.roll(b[k],1,axis=0),axis=1).max())
        # An overlap narrower than the linear tolerance is a near-contact, not
        # a resolved penetration. Use area/edge length, not a dimensionless epsilon.
        if area>tol*span:hits.append({'faces':pair[k].tolist(),'kind':'coplanar_overlap','area_mm2':float(area),'area_per_longest_edge_mm':float(area/span)})
    return hits

def self_test():
    a=np.array([[-1,-1,0],[1,-1,0],[0,1,0]],float)
    cases=[('penetration',np.array([[-.3,0,-1],[.3,0,1],[.3,.8,-1]]),True),
        ('nested_coplanar',a*.3,True),('shared_edge',np.array([[1,-1,0],[-1,-1,0],[0,-2,0]]),False),
        ('separated',a+[0,0,1],False)]
    for name,b,expected in cases:
        assert bool(inspect_pairs(np.array([a,b]),[[0,1]],1e-7))==expected,name
    print('triangle intersection controls: 4/4 PASS')

def main():
    p=argparse.ArgumentParser();p.add_argument('file',nargs='?');p.add_argument('--output');p.add_argument('--tolerance',type=float,default=1e-7);a=p.parse_args();self_test()
    if not a.file:return
    mesh=trimesh.load_mesh(a.file,process=True);triangles=mesh.triangles;tree=mesh.triangles_tree
    bounds=np.hstack((triangles.min(axis=1),triangles.max(axis=1)));pairs=[];hits=[];total=0;start=time.time()
    for i,bound in enumerate(bounds):
        pairs.extend((i,j) for j in tree.intersection(bound) if j>i)
        if len(pairs)>=12000 or i==len(bounds)-1:
            if pairs:hits.extend(inspect_pairs(triangles,pairs,a.tolerance));total+=len(pairs);pairs=[]
            if len(hits)>100:break
    report={'file':str(Path(a.file).resolve()),'triangles':len(triangles),'candidate_pairs':total,'tolerance_mm':a.tolerance,
        'proper_intersections':hits,'stopped_after_100_hits':len(hits)>100,'pass':not hits,'elapsed_seconds':time.time()-start,
        'scope':'Proper noncoplanar penetration and positive-area coplanar overlap. Point/edge contact alone is excluded; run topology audit separately. Numerical test, not an exact-arithmetic certificate.'}
    Path(a.output).write_text(json.dumps(report,indent=2),encoding='utf-8');print(json.dumps({k:v for k,v in report.items() if k!='proper_intersections'}))
if __name__=='__main__':main()
