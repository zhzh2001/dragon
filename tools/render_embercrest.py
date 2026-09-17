"""Render the real rig in a concept-like raised-wing pose, without changing GLB.

blender --background assets/embercrest-scripted/source/embercrest.blend \
  --python tools/render_embercrest.py
"""
import bpy
import math
from pathlib import Path
from mathutils import Vector, Quaternion

ROOT=Path(__file__).resolve().parents[1]
scene=bpy.data.scenes['Embercrest Workshop']
if bpy.context.window: bpy.context.window.scene=scene
scene.frame_set(1)
rig=bpy.data.objects['Embercrest_Rig']
rig.animation_data.action=None
for pb in rig.pose.bones:
    pb.rotation_mode='QUATERNION';pb.rotation_quaternion=Quaternion()
def rotate(name,axis,degrees):
    pb=rig.pose.bones[name]
    local_axis=pb.bone.matrix_local.to_quaternion().inverted() @ Vector(axis)
    pb.rotation_quaternion=Quaternion(local_axis,math.radians(degrees))
for side,suffix in [(1,'r'),(-1,'l')]:
    rotate('wing_root_'+suffix,(0,1,0),-side*35)
    rotate('wing_arm_'+suffix,(0,1,0),-side*9)
rotate('head',(0,0,1),-8)
camera=scene.camera
camera.location=(11,16,9)
camera.rotation_euler=(Vector((0,-.15,3.55))-camera.location).to_track_quat('-Z','Y').to_euler()
camera.data.ortho_scale=14.7
scene.render.resolution_x=1800;scene.render.resolution_y=1500
scene.cycles.samples=64
scene.render.filepath=str(ROOT/'artifacts/embercrest-scripted/concept-pose.png')
bpy.ops.render.render(write_still=True)
