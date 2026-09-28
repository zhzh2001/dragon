"""Author Sunspear from its own canonical coordinate plots and jaw crossings."""
import json
from pathlib import Path
root=Path(__file__).resolve().parents[1];m=json.loads((root/'artifacts/sunspear/measurement/mesh_measurements.json').read_text());b=[]
def bone(n,h,t,p):b.append([n,h,t,p])
def chain(names,pts,p):
 for i,n in enumerate(names):bone(n,pts[i],pts[i+1],p if i==0 else names[i-1])
bone('root',[0,-.023,.15],[0,.069,.164],None);bone('chest',[0,.069,.164],[0,.163,.168],'root')
chain(['neck_01','neck_02','neck_03'],[[0,.163,.168],[0,.216,.205],[0,.251,.258],[0,.294,.28]],'chest')
bone('head',[0,.294,.28],[0,.381,.275],'neck_03');bone('jaw',[0,.308,.257],[0,.348,.222],'head');bone('jaw_tip',[0,.348,.222],[0,.359,.217],'jaw')
chain([f'tail_{i:02d}' for i in range(1,9)],[[0,-.035,.151],[0,-.11,.13],[0,-.2,.119],[0,-.29,.113],[0,-.38,.111],[0,-.47,.11],[0,-.56,.108],[0,-.63,.108],[0,-.697,.107]],'root')
for sign,s in [(-1,'l'),(1,'r')]:
 def pt(v):return [sign*v[0],v[1],v[2]]
 chain(['wing_root_'+s,'wing_arm_'+s,'wing_wrist_'+s],list(map(pt,[[.046,.095,.207],[.085,.082,.224],[.111,.112,.278],[.142,.085,.282]])),'chest')
 for i,t in enumerate([[.522,-.35,.38],[.312,-.218,.271],[.091,-.064,.142]]):
  h=[.128,.095,.279];mid=[(h[k]+t[k])*.5 for k in range(3)];e=[t[k]+(t[k]-h[k])*.025 for k in range(3)]
  chain([f'wing_finger_{i+1:02d}_01_{s}',f'wing_finger_{i+1:02d}_02_{s}',f'wing_finger_{i+1:02d}_03_{s}'],list(map(pt,[h,mid,t,e])),'wing_wrist_'+s)
 chain(['upper_arm_'+s,'forearm_'+s,'wrist_'+s,'hand_'+s],list(map(pt,[[.055,.121,.186],[.068,.123,.103],[.062,.156,.036],[.064,.167,.019],[.07,.206,.011]])),'chest')
 chain(['thigh_'+s,'shin_'+s,'ankle_'+s,'foot_'+s],list(map(pt,[[.058,-.015,.146],[.066,-.015,.092],[.072,-.074,.059],[.069,-.07,.019],[.074,-.032,.011]])),'root')
 for fore,parent,y,x in [(True,'hand_'+s,.206,.07),(False,'foot_'+s,-.032,.074)]:
  for i,dx in enumerate([-.016,0,.017]):bone(f'toe_{"front" if fore else "hind"}_{i+1}_{s}',pt([x+dx,y-.018,.018]),pt([x+dx,y+.021,.009]),parent)
sk={'name':'Sunspear independently measured triangular-wing quadruped','source_sha256':m['source_sha256'],'reference_bounds':m['bounds'],'bones':b,'wing_bind_level_deg':0,'wing_field':{'span_x':[.072,.115],'span_z':[.13,.19],'fade_y':[.135,.18],'hub':[.128,.095,.279],'wrist_share':[.015,.075]},'jaw_mask':{'type':'plane','x_max':.043,'y_min':.304,'z_min':.208,'line':[[.304,.263],[.327,.26],[.35,.249],[.39,.244]],'hinge_blend':[.304,.328],'reclaim_above':True},'reference_span':{'axis':'x','metres':19},'preview':{'ortho_scale':1.7},'head_preview_scale':.23,'measurement_notes':'Own measured triangular membrane vertices and three fan ribs. Natural spread bind preserved with zero level correction. Mirror anatomy, preserve source geometric asymmetry.'}
(root/'tools/skeletons/sunspear.json').write_text(json.dumps(sk,indent=2)+'\n')
