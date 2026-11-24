"""Crop studio frames against an empty camera view and assemble the review sheets."""
import argparse
import sys

sys.dont_write_bytecode = True
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

from wingbeat_render import CACHE, REPORTS, STUDIES, frame_name, frames_dir, record_dir, views

FONT = ImageFont.truetype('/System/Library/Fonts/Menlo.ttc', 18)
BACKGROUND = (27, 31, 35)


def image_path(model, version, name):
    return frames_dir(model, version) / (name + '.jpg')


def load(model, version, name):
    with Image.open(image_path(model, version, name)) as image:
        return image.convert('RGB')


def bounds(image, plate):
    """Find the connected silhouette nearest the studio camera target."""
    w, h = image.size
    size = (w // 4, h // 4)
    def pixels(im):
        return np.array(im.resize(size, Image.Resampling.BOX), dtype=np.int16)
    delta = np.abs(pixels(image) - pixels(plate)).max(axis=2)
    mask = Image.fromarray(np.uint8(delta > 11) * 255)
    mask = mask.filter(ImageFilter.MedianFilter(3)).filter(ImageFilter.MaxFilter(5))
    ys, xs = np.where(np.asarray(mask))
    if not len(xs):
        raise ValueError('The background comparison found no dragon.')
    nearest = np.argmin((xs - size[0] / 2) ** 2 + (ys - size[1] / 2) ** 2)
    ImageDraw.floodfill(mask, (int(xs[nearest]), int(ys[nearest])), 128)
    ys, xs = np.where(np.asarray(mask) == 128)
    return (max(0, int(xs.min()) * 4 - 28), max(0, int(ys.min()) * 4 - 54),
            min(w, int(xs.max()) * 4 + 32), min(h, int(ys.max()) * 4 + 34))


def plate_for(model, group):
    orbit = group.replace('climb-', 'cruise-')
    return load(model, 'plate', orbit + '-0')


def union(boxes):
    boxes = list(boxes)
    return tuple(fn(b[k] for b in boxes) for k, fn in enumerate((min, min, max, max)))


def comparison(model, groups, output):
    layout = []
    for group in groups:
        frames = views(model)[group][1]
        plate = plate_for(model, group)
        cells = []
        for i, frame in enumerate(frames):
            name = frame_name(group, frames, i)
            box = union(bounds(load(model, v, name), plate) for v in ('before', 'after'))
            cells.append((frame, name, box))
        width = sum(b[2] - b[0] for _, _, b in cells)
        height = max(b[3] - b[1] for _, _, b in cells)
        layout.append((group, cells, width, height))
    # Keep each phase in the same column across the side and rear views.
    widths = [max(row[i][2][2] - row[i][2][0] for _, row, _, _ in layout)
              for i in range(len(layout[0][1]))]
    sheet = Image.new('RGB', (sum(widths), sum(2 * (e[3] + 32) for e in layout)), BACKGROUND)
    draw = ImageDraw.Draw(sheet)
    y = 0
    for group, cells, _, height in layout:
        for version, label in (('before', 'Procedural'), ('after', 'Keyframed')):
            x = 0
            for width, (frame, name, box) in zip(widths, cells):
                crop = load(model, version, name).crop(box)
                draw.text((x + 8, y + 4), f'{label}   {group}   {frame}', font=FONT)
                sheet.paste(crop, (x + (width - crop.width) // 2, y + 32))
                x += width
            y += height + 32
    sheet.save(record_dir(model) / output, quality=94, subsampling=0)


def overview(model):
    w, h, label = 320, 180, 30
    groups = views(model)
    rows = [g for g in groups if len(groups[g][1]) > 1 or g == 'glide']
    sheet = Image.new('RGB', (8 * w, (len(rows) + 1) * (h + label)), BACKGROUND)
    draw = ImageDraw.Draw(sheet)
    def cell(name, frame, row, col):
        x, y = col * w, row * (h + label)
        draw.text((x + 5, y + 3), f'{name}   {frame}', font=FONT)
        im = load(model, 'after', name)
        sheet.paste(im.resize((w, h), Image.Resampling.LANCZOS), (x, y + label))
    for row, group in enumerate(rows):
        frames = groups[group][1]
        for i, frame in enumerate(frames):
            cell(frame_name(group, frames, i), frame, row, i)
    for i, group in enumerate(g for g in groups if g not in rows):
        cell(group, groups[group][1][0], len(rows), i)
    sheet.save(record_dir(model) / 'views.jpg', quality=93)


def review(model, version, groups):
    output = CACHE / model / ('review-' + version)
    output.mkdir(exist_ok=True, parents=True)
    for group in groups:
        frames = views(model)[group][1]
        plate = plate_for(model, group)
        for i, _ in enumerate(frames):
            name = frame_name(group, frames, i)
            image = load(model, version, name)
            box = bounds(image, plate)
            image.crop(box).save(output / (name + '.jpg'), quality=95, subsampling=0)
            print(name, box)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('model', choices=(*REPORTS, *STUDIES))
    parser.add_argument('--review', choices=('before', 'after'))
    parser.add_argument('groups', nargs='*')
    args = parser.parse_intermixed_args()
    if args.review:
        review(args.model, args.review, args.groups)
    else:
        for mode in ('cruise', 'climb'):
            comparison(args.model, [mode + '-side', mode + '-rear'], mode + '-before-after.jpg')
        if args.model in STUDIES:
            comparison(args.model, ['report'], 'report-view-before-after.jpg')
        else:
            comparison(args.model, ['report-' + k for k in REPORTS[args.model]], 'report-views-before-after.jpg')
        overview(args.model)
