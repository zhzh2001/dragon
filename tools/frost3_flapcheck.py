"""Check body, ice and leg motion through a deep wing stroke."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from tide2_flapcheck import check

if __name__ == '__main__':
    travel = check('frost3', {
        'ruff': lambda x,y,z: (y > .15) & (z > .318),
        'tail spray': lambda x,y,z: y < -.54,
        'legs': lambda x,y,z: z < .1,
        'back': lambda x,y,z: (abs(x) < .04) & (y < .15) & (z > .18),
    })
    if travel > .02:
        raise SystemExit(1)
