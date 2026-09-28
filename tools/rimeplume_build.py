"""Species-scoped pipeline adapter; shared rigger remains untouched.

blender -b -t 4 --factory-startup --python tools/rimeplume_build.py
Optional -- sunspear uses Sunspear's independently measured skeleton.
Suppresses the shared flight-profile side effect; exports *-raw.glb.
"""
import sys
import hashlib
import json
sys.dont_write_bytecode=True
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
species=sys.argv[sys.argv.index('--')+1] if '--' in sys.argv else 'rimeplume'
assert species in ('rimeplume','sunspear')
candidate = ROOT / f'assets/{species}-cand-oneshot.glb'
measured = json.loads((ROOT / f'tools/skeletons/{species}.json').read_text())
if hashlib.sha256(candidate.read_bytes()).hexdigest() != measured['source_sha256']:
 raise ValueError('Candidate differs from measured skeleton; remeasure before rigging')
source=ROOT/'tools/rig_embercrest_candidate.py'
code=source.read_text()
def replace(old,new):
 global code
 assert code.count(old)==1,old
 code=code.replace(old,new)
replace("GLB = ROOT / ('assets/'+STEM+'.glb')", "GLB = ROOT / ('assets/'+STEM+'-raw.glb')")
a=code.index("GLB.with_suffix('.glb.flight.cfg').write_text(")
b=code.index("stats['reference_span']=SPAN",a)
code=code[:a]+"# Production profile writing intentionally omitted by scoped adapter.\n"+code[b:]
# Anatomy-specific hook: feather weights are not a shared membrane algorithm.
if species=='rimeplume':
 replace('# Trim to the runtime contract and explicitly normalize.', "exec((ROOT/'tools/rimeplume_feathers.py').read_text())\n# Trim to the runtime contract and explicitly normalize.")
if species == 'rimeplume':
 replace("stats['source_images']=source_images", "stats['source_images']=source_images\nstats['binding'] += '; rigid angular feather-ray field'")
a=code.index("for frame,name in [(1,'rest'),(21,'flap'),(41,'tuck'),(61,'jaw'),(81,'tail')]:")
code=code[:a]+"exec((ROOT/'tools/rimeplume_inspect.py').read_text())\n"
sys.argv=[str(source),'--','--input',f'assets/{species}-cand-oneshot.glb','--stem',species,'--skeleton',f'tools/skeletons/{species}.json','--keep-uvs','--target','80000']
exec(compile(code,str(source),'exec'),{'__file__':str(source),'__name__':'__main__'})
