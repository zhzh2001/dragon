"""Executed in scoped build namespace: full-size orthographic deform evidence."""
scene.render.engine='BLENDER_WORKBENCH'
scene.display.shading.light='STUDIO'
scene.display.shading.studio_light='paint.sl'
scene.display.shading.color_type='MATERIAL'
scene.display.shading.show_cavity=True
scene.display.shading.cavity_type='BOTH'
scene.render.resolution_x=1600;scene.render.resolution_y=1200
cam.hide_set(False)
center=Vector(tuple((lo[i]+hi[i])*.5 for i in range(3)))
extent=max(hi[i]-lo[i] for i in range(3))
camdata.ortho_scale=extent*1.35
for frame,label in [(1,'rest'),(21,'flap'),(41,'fold'),(61,'jaw')]:
 scene.frame_set(frame)
 for view,delta in [('side',(2,0,0)),('front',(0,2,0)),('rear',(0,-2,0)),('top',(0,0,2))]:
  if label=='jaw' and view not in ('side','front'):continue
  cam.location=center+Vector(delta)*extent
  cam.rotation_euler=(center-cam.location).to_track_quat('-Z','Y').to_euler()
  scene.render.filepath=str(CAP/f'{label}-{view}.png');bpy.ops.render.render(write_still=True)
# The head must be checked close, at rest and open without head yaw.
scene.frame_set(1)
rig.animation_data_clear()
for angle,label in [(0,'head-rest'),(-16,'head-open')]:
 reset();rotate('jaw',(1,0,0),angle);bpy.context.view_layer.update()
 head=rig.data.bones['head'];focus=(head.head_local+head.tail_local)*.5
 camdata.ortho_scale=SKEL['head_preview_scale']
 cam.location=focus+Vector((extent,0,0));cam.rotation_euler=(focus-cam.location).to_track_quat('-Z','Y').to_euler()
 scene.render.filepath=str(CAP/f'{label}.png');bpy.ops.render.render(write_still=True)
