"""Assemble native-size body comparisons and a labeled index of camera views."""
import sys

sys.dont_write_bytecode = True
from PIL import Image, ImageDraw, ImageFont

from hero_body_render import ART, VIEWS, EXTRA
from hero_flight_sheets import bounds, union

BG = (25, 29, 32)
FONT = ImageFont.truetype('/System/Library/Fonts/Menlo.ttc', 19)
GROUPS = {'beat': ('side','below'), 'glide': ('side','above'),
          'walk': ('side','above'), 'stand': ('front','above')}


def frame(version, group, i):
    return ART / version / f'{group}-{i}.jpg'


def comparison(motion):
    groups = {}
    cameras = [f'{motion}-{view}' for view in GROUPS[motion]]
    if motion == 'beat':
        cameras.append('climb-side')
    for group in cameras:
        boxes = []
        for i in range(8):
            for version in ('before','after'):
                with Image.open(frame(version,group,i)) as im:
                    boxes.append(bounds(im))
        # Use one crop across the strip to preserve the camera and the tail's travel.
        groups[group] = union(boxes)
    width = max(b[2]-b[0] for b in groups.values()) + 4
    height = 2*sum(b[3]-b[1]+38 for b in groups.values())
    sheet = Image.new('RGB',(8*width,height),BG)
    draw = ImageDraw.Draw(sheet)
    y = 0
    for group,box in groups.items():
        w,h = box[2]-box[0],box[3]-box[1]
        for version,label in [('before','Procedural'),('after','Keyframed')]:
            for i,f in enumerate(VIEWS[group][1]):
                x = i*width+(width-w)//2
                with Image.open(frame(version,group,i)) as im:
                    sheet.paste(im.crop(box),(x,y+38))
                draw.text((x+8,y+7),f'{label}  |  {group}  |  frame {f}',font=FONT,fill='white')
            y += h+38
    sheet.save(ART/(motion+'-before-after.jpg'),quality=94,subsampling=0)


def overview():
    w,h,label = 352,198,32
    rows = len(VIEWS) + (len(EXTRA)+7)//8
    sheet = Image.new('RGB',(8*w,rows*(h+label)),BG)
    draw = ImageDraw.Draw(sheet)
    for row,(group,(_,frames)) in enumerate(VIEWS.items()):
        for i,f in enumerate(frames):
            y = row*(h+label)
            with Image.open(frame('after',group,i)) as im:
                sheet.paste(im.resize((w,h),Image.Resampling.LANCZOS),(i*w,y+label))
            draw.text((i*w+6,y+5),f'{group}  {f}',font=FONT,fill='white')
    for i,(group,(_,frames)) in enumerate(EXTRA.items()):
        y = (len(VIEWS)+i//8)*(h+label)
        x = i%8*w
        with Image.open(frame('after',group,0)) as im:
            sheet.paste(im.resize((w,h),Image.Resampling.LANCZOS),(x,y+label))
        draw.text((x+6,y+5),f'{group}  {frames[0]}',font=FONT,fill='white')
    sheet.save(ART/'views.jpg',quality=92)


if __name__ == '__main__':
    for motion in sys.argv[1:] or [*GROUPS,'overview']:
        if motion == 'overview':
            overview()
        else:
            comparison(motion)
