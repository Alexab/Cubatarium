"""Compare two native B diagnostics and plot Walk mesh-floor envelopes.

Floor overlap candidates are diagnostics, never automatic art decisions.
Requires matplotlib only for this research chart, not creature generation.
"""
from pathlib import Path
import argparse
import json
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--out',type=Path,default=Path('docs/research/creature-r6-v2/bone-motion'))
out=parser.parse_args().out
a=json.loads((out/'baseline.json').read_text());b=json.loads((out/'current.json').read_text())
assert len(a['resources'])==len(b['resources'])==17
before={r['id']:r for r in a['resources']};rows=[];regressions=[]
for current in b['resources']:
 sid=current['id'];reference=before[sid]
 old={(s['state'],s['speed_ratio']):s for s in reference['scenarios']}
 diffs=[]
 for s in current['scenarios']:
  r=old[s['state'],s['speed_ratio']]
  if s['ground_contact_candidate']:
   diffs.append({'state':s['state'],'speed_ratio':s['speed_ratio'],'lowest_y_before':r['lowest_y'],'lowest_y_after':s['lowest_y'],'highest_floor_y_before':r['highest_floor_y'],'highest_floor_y_after':s['highest_floor_y']})
   if s['lowest_y'] < -.005 and s['lowest_y']<r['lowest_y']-.005:
    regressions.append({'id':sid,'state':s['state'],'speed_ratio':s['speed_ratio'],'penetration_delta':s['lowest_y']-r['lowest_y']})
 rows.append({'id':sid,'profile_before':reference['profile'],'profile_after':current['profile'],'ground_scenarios':diffs})
assert json.loads((out/'baseline_context.json').read_text())['exe_sha256']==json.loads((out/'current_context.json').read_text())['exe_sha256']
(out/'comparison.json').write_text(json.dumps({'schema':'creature-native-bone-floor-comparison.v1','same_native_release_executable':True,'scope':'Diagnostic floor bounds; positive clearance may be intentional flight/hop. Not a stance/stride or full R6 verdict.','possible_ground_penetration_regressions_over_0_005':regressions,'resources':rows},indent=2)+'\n')
print('Ground penetration deltas > .005:',len(regressions),'species',sorted({r['id'] for r in regressions}))
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
items=[r for r in rows if r['ground_scenarios']]
fig,ax=plt.subplots(figsize=(13,6))
for i,r in enumerate(items):
 s=next(s for s in r['ground_scenarios'] if s['state']=='Walk' and s['speed_ratio']==1)
 for shift,kind,color in [(-.13,'before','#787f87'),(.13,'after','#206d9b')]:
  low=s['lowest_y_'+kind];high=s['highest_floor_y_'+kind]
  ax.vlines(i+shift,low,high,color=color,linewidth=5,label=('Original' if kind=='before' else 'Current') if i==0 else None)
  ax.plot([i+shift],[low],marker='_',color=color,markersize=12)
  ax.plot([i+shift],[high],marker='_',color=color,markersize=12)
ax.axhline(0,color='#222',linestyle='--',linewidth=1)
ax.set_xticks(range(len(items)),[r['id'] for r in items],rotation=35,ha='right')
ax.set_ylabel('Lowest rendered mesh vertex Y, blocks')
ax.set_title('Native B pose engine: Walk at walk_speed, 1025 phases over 0..4π\nRange of mesh floor; rabbit hop is intentional, not a contact failure')
ax.legend();ax.grid(axis='y',alpha=.2);fig.tight_layout();fig.savefig(out/'walk_floor_ranges.png',dpi=150)
plt.close(fig)

