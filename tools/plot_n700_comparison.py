"""Plot measured before/after STL sections; no source photo is redistributed."""
import argparse,json
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

p=argparse.ArgumentParser();p.add_argument('--before',required=True)
p.add_argument('--after',required=True);p.add_argument('--notes',required=True)
p.add_argument('--output',required=True);a=p.parse_args()
before=json.loads(Path(a.before).read_text());after=json.loads(Path(a.after).read_text())
notes=json.loads(Path(a.notes).read_text());out=Path(a.output);out.mkdir(parents=True,exist_ok=True)
fig,axes=plt.subplots(1,2,figsize=(13,6.5));fig.patch.set_facecolor('#f6f8fb')
for ax in axes:
    ax.set_facecolor('white');ax.grid(alpha=.18);ax.spines[['right','top']].set_visible(False)
for data,label,color,style in ((before,'Before: high-degree solid loft','#c3444d','--'),
                               (after,'After: cubic solid loft','#087b82','-')):
    rows=data['dense_sections'];x=[s['x_mm'] for s in rows]
    axes[0].plot(x,[s['y_max']-s['y_min'] for s in rows],style,color=color,lw=2,label=label)
    axes[1].plot(x,[s['z_max'] for s in rows],style,color=color,lw=2,label=label)
stations=notes['sections_mm']
axes[0].scatter([s[0] for s in stations],[2*s[1] for s in stations],s=25,c='#263449',zorder=3,label='Input reference sections')
axes[0].axhline(22.4,color='#63738c',lw=1,label='Published scaled body width')
axes[0].set(xlim=(45,64.3),ylim=(21,23.25),xlabel='Distance from nose tip (mm)',ylabel='Full cross-section width (mm)',title='Unintended widening removed')
axes[1].scatter([s[0] for s in stations],[s[4] for s in stations],s=25,c='#263449',zorder=3)
axes[1].set(xlim=(52,64.3),ylim=(23.55,24.08),xlabel='Distance from nose tip (mm)',ylabel='Roof height (mm)',title='Local roof dip removed')
axes[0].legend(loc='lower right',fontsize=8,framealpha=.9)
old_width=2*before['bounds_mm'][1][1];new_width=2*after['bounds_mm'][1][1]
roof=[s['z_max'] for s in before['dense_sections']];high=roof[0];dip=0
for z in roof:high=max(high,z);dip=max(dip,high-z)
fig.suptitle('N700 nose: numerical inspection found what the section stations missed',fontsize=15,y=.97)
fig.text(.07,.16,f'Maximum width: {old_width:.4f} -> {new_width:.4f} mm    |    Roof dip: {dip:.4f} -> {after["maximum_unintended_roof_dip_mm"]:.4f} mm',fontsize=12,weight='bold',color='#163748')
fig.text(.07,.10,f'129 dense slices; 1,782 distances to reference sections. After: maximum distance {after["maximum_distance_mm"]:.5f} mm.',fontsize=10)
fig.text(.07,.055,'Reference sections are photo estimates. The published envelope sets scale; these errors measure CAD/export fidelity, not accuracy to a factory model.',fontsize=8.5,color='#526174')
fig.subplots_adjust(top=.85,bottom=.28,left=.07,right=.97,wspace=.25)
fig.savefig(out/'n700-before-after.png',dpi=180);fig.savefig(out/'n700-before-after.svg')
