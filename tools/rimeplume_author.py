"""Author Rimeplume from its measured orthographic plots and jaw ray crossings."""
import json
from pathlib import Path
root=Path(__file__).resolve().parents[1]
m=json.loads((root/'artifacts/rimeplume/measurement/mesh_measurements.json').read_text())
b=[]
def bone(n,h,t,p):b.append([n,h,t,p])
def chain(names,pts,p):
 for i,n in enumerate(names):bone(n,pts[i],pts[i+1],p if i==0 else names[i-1])
bone('root',[0,0,.205],[0,.095,.218],None)
bone('chest',[0,.095,.218],[0,.185,.238],'root')
chain(['neck_01','neck_02','neck_03'],[[0,.185,.238],[0,.215,.285],[0,.224,.337],[0,.235,.389]],'chest')
bone('head',[0,.235,.389],[0,.327,.379],'neck_03')
bone('jaw',[0,.254,.369],[0,.308,.339],'head');bone('jaw_tip',[0,.308,.339],[0,.316,.337],'jaw')
chain([f'tail_{i:02d}' for i in range(1,9)],[[0,-.012,.202],[0,-.092,.166],[0,-.18,.132],[0,-.265,.109],[0,-.35,.086],[0,-.44,.075],[0,-.52,.079],[0,-.605,.09],[0,-.694,.115]],'root')
# Primary and secondary fan rays measured in the frontal/top plots; a ray
# carries one rigid vane group, with an unweighted tip joint for runtime aim.
tips=[[.541,-.067,.465],[.535,-.102,.421],[.54,-.124,.379],[.47,-.128,.30],[.401,-.107,.258],[.34,-.08,.214],[.27,-.052,.164],[.185,-.024,.123]]
for sign,s in [(-1,'l'),(1,'r')]:
 def pt(v):return [sign*v[0],v[1],v[2]]
 chain(['wing_root_'+s,'wing_arm_'+s,'wing_wrist_'+s],list(map(pt,[[.055,.125,.28],[.141,.085,.331],[.223,.066,.411],[.257,.042,.423]])),'chest')
 for i,t in enumerate(tips):
  h=[.235,.056,.411];delta=[t[k]-h[k] for k in range(3)];e=[t[k]+delta[k]*.025 for k in range(3)]
  chain([f'wing_finger_{i+1:02d}_01_{s}',f'wing_finger_{i+1:02d}_02_{s}'],list(map(pt,[h,t,e])),'wing_wrist_'+s)
 chain(['upper_arm_'+s,'forearm_'+s,'wrist_'+s,'hand_'+s],list(map(pt,[[.07,.193,.214],[.098,.183,.12],[.096,.203,.041],[.099,.225,.02],[.102,.263,.012]])),'chest')
 chain(['thigh_'+s,'shin_'+s,'ankle_'+s,'foot_'+s],list(map(pt,[[.065,.023,.205],[.086,.043,.126],[.101,-.018,.083],[.097,-.013,.025],[.101,.02,.013]])),'root')
 for fore,parent,y,x in [(True,'hand_'+s,.263,.102),(False,'foot_'+s,.02,.101)]:
  for i,dx in enumerate([-.018,0,.02]):bone(f'toe_{"front" if fore else "hind"}_{i+1}_{s}',pt([x+dx,y-.02,.021]),pt([x+dx,y+.027,.01]),parent)
sk={'name':'Rimeplume independently measured feathered quadruped','source_sha256':m['source_sha256'],'reference_bounds':m['bounds'],'bones':b,'wing_bind_level_deg':0,'wing_field':None,'feather_field':{'hub':[.235,.056,.411],'span_x':[.095,.16],'span_z':[.105,.19],'fade_y':[.13,.18],'angular_blend_radians':.025,'note':'Angular ray ownership is constant along vane length; only root gate blends to heat. No flexible membrane distance field.'},'jaw_mask':{'type':'plane','x_max':.048,'y_min':.252,'z_min':.328,'line':[[.252,.370],[.28,.369],[.30,.363],[.32,.355],[.34,.350]],'hinge_blend':[.252,.285],'reclaim_above':True},'reference_span':{'axis':'x','metres':19},'preview':{'ortho_scale':1.7},'head_preview_scale':.29,'measurement_notes':'Own 1600x1200 canonical plots. Mirror anatomy across X; source asymmetry retained. Raised natural bird wing retained; no inherited 35 degree level correction.'}
(root/'tools/skeletons/rimeplume.json').write_text(json.dumps(sk,indent=2)+'\n')
