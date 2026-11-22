"""Measure Blight2 body travel and the haunch beside the tail root."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from tide2_flapcheck import check

if __name__ == '__main__':
    worst = check('blight2', {
        'left upper haunch': lambda x, y, z: (x < -.035) & (y < -.016) & (y > -.15) & (z > .13) & (z < .235),
    })
    if worst > .02:
        raise SystemExit(1)
