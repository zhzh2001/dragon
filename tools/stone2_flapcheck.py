"""Measure Stone2 body travel during a wing-only stroke."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from tide2_flapcheck import check

if __name__ == '__main__':
    worst = check('stone2', {
        'upper back and crystals': lambda x, y, z: (abs(x) < .14) & (y > -.12) & (y < .18) & (z > .26),
        'forelegs': lambda x, y, z: (abs(x) > .035) & (y > .065) & (y < .20) & (z < .19),
    })
    if worst > .02:
        raise SystemExit(1)
