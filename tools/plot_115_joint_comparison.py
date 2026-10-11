"""Independent cross-sections of the actual exported STL files, not illustrations."""
import argparse, json
from pathlib import Path
import numpy as np
import trimesh
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.collections import LineCollection

p=argparse.ArgumentParser()
p.add_argument('--before',required=True);p.add_argument('--after',required=True)
p.add_argument('--output',required=True);a=p.parse_args()
fig,axes=plt.subplots(2,2,figsize=(12,7.5))
for column,(path,color,label,count) in enumerate(((a.before,'#c3423f','Before: wrong wall direction',2),
                                               (a.after,'#176d9c','After: material inside body',0))):
    mesh=trimesh.load_mesh(path,process=False)
    section=trimesh.intersections.mesh_plane(mesh,[1,0,0],[100,0,0])[:,:,[1,2]]
    for row in range(2):
        ax=axes[row,column]
        ax.add_collection(LineCollection(section,colors=color,linewidths=1.4 if row==0 else 2))
        ax.set_aspect('equal');ax.grid(alpha=.2)
        ax.set_xlabel('Y (mm)');ax.set_ylabel('Z (mm)')
        if row==0:
            ax.set_xlim(-17,17);ax.set_ylim(7,40)
            ax.set_title(f'{label}\nFull STL non-manifold edges: {count}')
        else:
            ax.set_xlim(-15.6,-13.1);ax.set_ylim(30.5,33)
            ax.set_title('Left wall / roof joint, enlarged')
            ax.plot([-14.5],[31.5],'o',color='#30343b',markersize=4)
fig.suptitle('115-series: section at X = 100 mm from exported STL',fontsize=16,y=.98)
fig.text(.5,.012,'Nominal wall 0.8 mm. Body reference width 29 mm; door relief adds 0.18 mm per side.\n'
         'This verifies the modeled geometry, not factory-drawing accuracy.',ha='center',fontsize=9)
fig.tight_layout(rect=[0,.065,1,.94])
out=Path(a.output);out.parent.mkdir(parents=True,exist_ok=True)
fig.savefig(out,dpi=160)
print(out)
