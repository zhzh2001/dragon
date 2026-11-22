"""Check the flank, hind legs, crown and rudder during a wing-only stroke."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from tide2_flapcheck import check

if __name__ == '__main__':
    travel = check('storm3', {
        'crest and horns': lambda x, y, z: (y > .15) & (z > .46),
        'hind legs': lambda x, y, z: (y > .04) & (z < .22),
        'flank at the web': lambda x, y, z: (abs(x) < .08) & (y > .055) & (y < .17) & (z > .22) & (z < .33),
        'tail and rudder': lambda x, y, z: y < -.025,
    })
    if travel > .02:
        raise SystemExit(1)
