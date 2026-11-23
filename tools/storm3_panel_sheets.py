"""Compare the same wing pixels before and after the repair."""
from PIL import Image, ImageDraw, ImageFont
from storm3_panel_captures import OUTPUT, views

BACKGROUND = (25, 29, 35)
WHITE = (236, 239, 242)
FONT = ImageFont.truetype('/System/Library/Fonts/Menlo.ttc', 17)

# Each box follows one inner panel from its rib to its flank for a whole beat.
CROPS = {
    'report': (350, 211, 1130, 675),
    'panel-above': (14, 232, 794, 909),
    'panel-below': (110, 157, 890, 756),
    'report-low': (385, 199, 1165, 663),
}


def draw_cell(sheet, image, x, y, label):
    ImageDraw.Draw(sheet).text((x + 8, y + 5), label, font=FONT, fill=WHITE)
    sheet.paste(image, (x, y + 31))


def comparison():
    gutter = 4
    width = max(box[2] - box[0] for box in CROPS.values()) + gutter
    height = sum(2 * (box[3] - box[1] + 31 + gutter) for box in CROPS.values())
    sheet = Image.new('RGB', (width * 8, height), BACKGROUND)
    frames = {name: frame for name, _, _, frame in views()}
    y = 0
    for group, box in CROPS.items():
        cell_width = box[2] - box[0] + gutter
        for version in ('before', 'after'):
            for i in range(8):
                name = f'{group}-{i}'
                with Image.open(OUTPUT / version / (name + '.jpg')) as image:
                    cropped = image.crop(box)
                draw_cell(sheet, cropped, i * cell_width, y, f'{group}   {frames[name]}   {version}')
            y += box[3] - box[1] + 31 + gutter
    sheet.save(OUTPUT / 'panel-before-after.jpg', quality=95, subsampling=0)


def overview():
    width, height = 320, 180
    collection = views()
    sheet = Image.new('RGB', (8 * width, ((len(collection) + 7) // 8) * (height + 31)), BACKGROUND)
    for i, (name, _, _, frame) in enumerate(collection):
        with Image.open(OUTPUT / 'after' / (name + '.jpg')) as image:
            small = image.resize((width, height), Image.Resampling.LANCZOS)
        draw_cell(sheet, small, (i % 8) * width, (i // 8) * (height + 31), f'{name}   {frame}')
    sheet.save(OUTPUT / 'views.jpg', quality=92)


if __name__ == '__main__':
    comparison()
    overview()
