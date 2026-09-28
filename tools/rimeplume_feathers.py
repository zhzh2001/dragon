"""Executed by rimeplume_build in rigger namespace: constant-along-ray vanes.
The generated mesh fuses overlapping vanes. Use frontal angular sectors about
its measured wrist hub, not distance-to-segment fields varying along feathers.
Distal tip joints are deliberately unweighted; each fan ray has one rigid bone.
"""
F=SKEL['feather_field'];hub=Vector(F['hub']);changed=0
for v in body.data.vertices:
 co=canonical(v.co);x,y,z=co
 blend=smoothstep(*F['span_x'],abs(x))*smoothstep(*F['span_z'],z)*(1-smoothstep(*F['fade_y'],y))
 if blend<=0:continue
 suffix='l' if x<0 else 'r'
 theta=math.atan2(z-hub.z,abs(x)-hub.x)
 rays=[]
 for n,a,b in wing_bones[suffix]:
  if not n.startswith('wing_finger_') or '_01_' not in n:continue
  angle=math.atan2(b.z-hub.z,abs(b.x)-hub.x)
  distance=abs(math.atan2(math.sin(theta-angle),math.cos(theta-angle)))
  rays.append((distance,n))
 rays.sort();d0,n0=rays[0];d1,n1=rays[1]
 # Only the narrow angular seam blends; all points on a shaft share weights.
 mix=.5*(1-smoothstep(0,F['angular_blend_radians'],d1-d0))
 field={n0:1-mix,n1:mix}
 # Before the wrist, preserve the wing arm's heat weights in the fleshy
 # leading edge. Behind it the rigid secondary rays carry the feather vane.
 front_height=.285+(abs(x)-.06)*.72
 if abs(x)<hub.x:blend*=1-smoothstep(front_height-.035,front_height+.005,z)
 combined={body.vertex_groups[g.group].name:g.weight*(1-blend) for g in v.groups}
 for n,w in field.items():combined[n]=combined.get(n,0)+w*blend
 final=continuous_top_four(combined)
 for gi in [g.group for g in v.groups]:body.vertex_groups[gi].remove([v.index])
 for n,w in final.items():body.vertex_groups[n].add([v.index],w,'REPLACE')
 changed+=1
print('FEATHER_RAY_FIELD',changed,'vertices; rigid ray weights; no radial bending',flush=True)
