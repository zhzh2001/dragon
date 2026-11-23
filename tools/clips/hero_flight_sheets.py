"""Crop paired studio frames at native resolution and assemble the review sheets."""
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

from hero_flight_render import ART, CACHE, VIEWS, BEAT

FONT = ImageFont.truetype('/System/Library/Fonts/Menlo.ttc', 21)
BG = (24, 27, 31)


def bounds(im):
    # Join neighbouring red scales on a coarse mask. Keep the central creature.
    w, h = im.size
    small = np.asarray(im.resize((w//4,h//4))).astype(int)
    r,g,b = small.transpose(2,0,1)
    mask = Image.fromarray(((r > g*1.19+3) & (r > b*1.15+2)).astype('uint8')*255)
    mask = mask.filter(ImageFilter.MaxFilter(7))
    seen = np.asarray(mask)
    ys,xs = np.where(seen)
    if not len(xs):
        return (0,0,w,h)
    best = ((xs-w/8)**2 + (ys-h/8)**2).argmin()
    ImageDraw.floodfill(mask, (int(xs[best]),int(ys[best])), 127)
    yy,xx = np.where(np.asarray(mask)==127)
    return (max(0,4*int(xx.min())-20),max(0,4*int(yy.min())-20),
            min(w,4*int(xx.max())+24),min(h,4*int(yy.max())+24))


def union(boxes):
    return (min(b[0] for b in boxes), min(b[1] for b in boxes),
            max(b[2] for b in boxes), max(b[3] for b in boxes))


def load(version, name):
    return Image.open(ART/version/(name+'.jpg')).convert('RGB')


def review(version, group):
    frames = VIEWS[group][1]
    images = [load(version, group if group == 'glide' else f'{group}-{i}') for i in range(len(frames))]
    for i in range(0,len(images),2):
        pair = images[i:i+2]
        boxes = [bounds(im) for im in pair]
        crops = [im.crop(b) for im,b in zip(pair,boxes)]
        canvas = Image.new('RGB',(sum(im.width for im in crops),max(im.height for im in crops)+36),BG)
        draw = ImageDraw.Draw(canvas)
        x = 0
        for j,im in enumerate(crops):
            draw.text((x+5,3),f'{version} {group}  {frames[i+j]}',font=FONT)
            canvas.paste(im,(x,36))
            x += im.width
        canvas.save(CACHE/f'{version}-{group}-pair-{i//2}.jpg',quality=95)


def comparison(mode):
    rows = []
    for view in ('side','rear'):
        pairs = [[load(v,f'{mode}-{view}-{i}') for v in ('before','after')] for i in range(8)]
        boxes = [union([bounds(im) for im in pair]) for pair in pairs]
        for v,label in enumerate(('Procedural','Keyframed')):
            cells = [pair[v].crop(box) for pair,box in zip(pairs,boxes)]
            row = Image.new('RGB',(sum(im.width for im in cells),max(im.height for im in cells)+42),BG)
            draw = ImageDraw.Draw(row)
            x = 0
            for i,im in enumerate(cells):
                draw.text((x+10,7),f'{label}   {mode} / {view}   {BEAT[i]}',font=FONT)
                row.paste(im,(x,42))
                x += im.width
            rows.append(row)
    sheet = Image.new('RGB',(max(r.width for r in rows),sum(r.height for r in rows)),BG)
    y=0
    for row in rows:
        sheet.paste(row,(0,y))
        y += row.height
    sheet.save(ART/f'{mode}-before-after.jpg',quality=94,subsampling=0)


def overview():
    w,h,label = 352,198,34
    sheet = Image.new('RGB',(8*w,len(VIEWS)*(h+label)),BG)
    draw = ImageDraw.Draw(sheet)
    for row,(group,(_,frames)) in enumerate(VIEWS.items()):
        for i,frame in enumerate(frames):
            name = group if group == 'glide' else f'{group}-{i}'
            im = load('after',name)
            x,y = i*w,row*(h+label)
            draw.text((x+5,y+3),f'{group}  {frame}',font=FONT)
            sheet.paste(im.resize((w,h),Image.Resampling.LANCZOS),(x,y+label))
    sheet.save(ART/'views.jpg',quality=94)


if __name__ == '__main__':
    if len(sys.argv)>1:
        for group in sys.argv[2:]:
            review(sys.argv[1],group)
    else:
        for mode in ('cruise','climb'):
            comparison(mode)
        overview()
