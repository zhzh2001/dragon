"""Capture the requested engine views without changing the engine."""
import pathlib
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "artifacts/hero-claws"
ENGINE = "~/src/game-claude/build/dragon"
VIEWS = {
    "feedback-root": ["--studio", "0", "--inspect", "275", "10.5", "3", "--frames", "483"],
    "feedback-wrist": ["--studio", "1", "--inspect", "222", "11.5", "7", "--frames", "2185"],
    "wrist-bind": ["--bind-pose", "--inspect", "200", "9", "25", "--frames", "20"],
    "wrist-glide": ["--studio", "0", "--inspect", "200", "9", "25", "--frames", "60"],
    "root-glide": ["--studio", "0", "--inspect", "300", "9", "-10", "--frames", "120"],
    "root-flap": ["--studio", "1", "--inspect", "300", "9", "-10", "--frames", "330"],
    "standing": ["--studio", "9", "--inspect", "60", "11", "3", "--frames", "200"],
}

phase = sys.argv[1]
for name, args in VIEWS.items():
    bmp = OUT / f"{phase}-{name}.bmp"
    jpg = OUT / f"{phase}-{name}.jpg"
    command = [ENGINE, "--model", str(ROOT / "assets/embercrest-hero.glb"),
               "--size", "1920x1080", "--headless", "--hide-ui", *args,
               "--screenshot", str(bmp)]
    for attempt in range(3):
        with (OUT / f"{phase}-{name}.log").open("w") as log:
            result = subprocess.run(command, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT)
        if result.returncode == 0 and bmp.exists():
            break
        if attempt == 2:
            raise RuntimeError(f"Engine capture failed: {name}")
        time.sleep(10)
    subprocess.run(["sips", "-s", "format", "jpeg", str(bmp), "--out", str(jpg)],
                   stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, check=True)
    bmp.unlink()
    print(jpg.name, flush=True)
