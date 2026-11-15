"""Place matched wing crops side by side without scaling."""
import pathlib
import subprocess
import sys

OUT = pathlib.Path(__file__).resolve().parent
CROPS = {
    "feedback-root": "620x430+420+180",
    "feedback-wrist": "850x400+650+280",
    "wrist-bind": "770x600+1150+190",
    "wrist-glide": "770x500+1150+230",
    "root-glide": "800x450+400+160",
    "root-flap": "800x500+400+110",
    "standing": "720x580+720+260",
}

after = sys.argv[1] if len(sys.argv) > 1 else "after"
prefix = "" if after == "after" else after + "-"
for name, crop in CROPS.items():
    command = ["magick"]
    for phase in ("before", after):
        command += ["(", str(OUT / f"{phase}-{name}.jpg"), "-crop", crop, "+repage", ")"]
    command += ["+append", "-quality", "95", str(OUT / f"{prefix}pair-{name}.jpg")]
    subprocess.run(command, stdin=subprocess.DEVNULL, check=True)
    print(f"pair-{name}.jpg: {crop}, before left, after right")
