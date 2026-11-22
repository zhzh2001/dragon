"""Fit Storm3's measured wyvern skeleton and bind the tail rudder."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from frost3_rig import build

if __name__ == '__main__':
    build('storm3')
