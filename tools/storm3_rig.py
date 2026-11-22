"""Fit Storm3's standing skeleton before grading and material repair.

After material repair, bake both standing GLBs with storm3_flight_bind.py.
"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from frost3_rig import build

if __name__ == '__main__':
    build('storm3')
