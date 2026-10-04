#!/usr/bin/env python3
"""Compose local benchmark/flat/native plates without baking reference lighting."""
from pathlib import Path
from PIL import Image, ImageDraw
cap=Path('artifacts/dragothic-rig/pass2');ref=Path('assets/alt/3dmark2001/reference')
for name,frame,flat,native in [('head','031','front','head-front'),('side','024','side','side'),('horns','032','head','head-top')]:
    plate=Image.new('RGB',(3072,800),(28,28,28));draw=ImageDraw.Draw(plate)
    for i,(path,label) in enumerate([(ref/f'dragothic-high-{frame}.png',f'Benchmark High {frame}'),(cap/f'repaired-albedo-{flat}.png','Source-layer albedo; unlit'),(cap/f'{native}.png','Native engine; warm sun; no post')]):
        with Image.open(path) as source:
            source=source.convert('RGB');source.thumbnail((1024,768));plate.paste(source,(i*1024+(1024-source.width)//2,32+(768-source.height)//2))
        draw.text((i*1024+12,10),label,fill='white')
    plate.save(cap/f'compare-albedo-{name}.png')
