"""Build the independently measured Sunspear rig using the scoped adapter."""
import sys
from pathlib import Path
sys.dont_write_bytecode=True
sys.argv=[sys.argv[0],'--','sunspear']
exec(compile(Path(__file__).with_name('rimeplume_build.py').read_text(),str(Path(__file__).with_name('rimeplume_build.py')),'exec'))
