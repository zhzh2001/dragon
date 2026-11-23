"""Damage scratch copies and require the checker to reject each specific defect."""
import subprocess
import sys

sys.dont_write_bytecode = True
from check_hero_flight import CLIP, ROOT
from check_clip import read_glb
from hero_flight_glb import pack

CACHE = ROOT/'build-clip-cache'


def corrupt(group, label):
    doc, binary = read_glb(CLIP)
    data = bytearray(binary)
    anim = doc['animations'][0]
    def track(name):
        i = next(i for i,n in enumerate(doc['nodes']) if n['name']==name)
        channel = next(c for c in anim['channels'] if c['target']['node']==i)
        output = anim['samplers'][channel['sampler']]['output']
        acc = doc['accessors'][output]
        start = doc['bufferViews'][acc['bufferView']]['byteOffset']
        return start, channel
    if label == 'name':
        anim['name'] = 'broken'
    elif label == 'rest':
        doc['nodes'][0]['translation'][0] += .01
    elif label == 'channel':
        track('wing_root_r')[1]['target']['path'] = 'scale'
    elif label == 'body':
        track('wing_root_r')[1]['target']['node'] = 0
    elif label == 'samples':
        doc['accessors'][0]['count'] -= 1
    else:
        if label in ('seam','mirror','step'):
            start,_ = track('wing_arm_r')
            a,b = {'seam':(60,15),'mirror':(20,22),'step':(22,49)}[label]
            data[start+a*16:start+(a+1)*16] = binary[start+b*16:start+(b+1)*16]
        else:
            # Shift a selected chain in time or substitute the cruise amplitudes.
            if label == 'amplitude':
                doc['animations'][1]['samplers'] = anim['samplers']
            else:
                for ch in anim['channels']:
                    name = doc['nodes'][ch['target']['node']]['name']
                    if label == 'lag' and not name.startswith('wing_finger_'):
                        continue
                    if label == 'fold' and not name.startswith('wing_wrist_'):
                        continue
                    start,_ = track(name)
                    for i in range(61):
                        j = (i + 12) % 60
                        data[start+i*16:start+(i+1)*16] = binary[start+j*16:start+(j+1)*16]
    path = CACHE / f'bad-{label}.glb'
    path.write_bytes(pack(doc,data))
    outcome = subprocess.run([sys.executable, '-B', str(ROOT/'tools/clips/check_hero_flight.py'), str(path)],
                             capture_output=True,text=True)
    line = next((s for s in outcome.stdout.splitlines() if s.startswith(f'FAIL {group} ')), None)
    if outcome.returncode != 1 or not line:
        raise RuntimeError(f'Checker missed {label}.\n{outcome.stdout}')
    print(f'PASS {label}: exit 1, {line}')


if __name__ == '__main__':
    CACHE.mkdir(exist_ok=True)
    for group,label in [(1,'name'),(1,'samples'),(2,'rest'),(2,'channel'),(2,'body'),(3,'seam'),
                        (4,'mirror'),(5,'phase'),(6,'lag'),(7,'fold'),(8,'step'),(9,'amplitude')]:
        corrupt(group,label)
