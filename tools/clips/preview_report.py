#!/usr/bin/env python3
"""Assemble Blender renders and joint-angle traces with Pillow."""
import json
import math
from pathlib import Path
import sys

from PIL import Image, ImageDraw, ImageFont

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
ART = ROOT / 'artifacts/clip-trial'
FONT = '/System/Library/Fonts/Supplemental/Arial.ttf'
COLORS = ['#367dd9', '#d95d39', '#339967', '#9e55c3', '#bd921f', '#d74e91', '#239ca2', '#73543e']


def font(size):
    return ImageFont.truetype(FONT, size)


def sheets(manifest):
    for clip in ('brake_bank_l', 'brake_bank_r'):
        canvas = Image.new('RGB', (1920, 2150), '#111923')
        draw = ImageDraw.Draw(canvas)
        draw.text((20, 12), f'{clip} | 2.4 s loop | overlay on bind pose', font=font(28), fill='#f0f4fa')
        draw.text((20, 49), 'AI-generated Embercrest mesh and clip study. Procedural brake flare and bank are absent.',
                  font=font(18), fill='#aebdce')
        for view_index, view in enumerate(('side', 'front', 'below')):
            top = 85 + view_index * 678
            draw.text((20, top), view.upper(), font=font(24), fill='#dce7f3')
            entries = [r for r in manifest['frames'] if r['clip'] == clip and r['view'] == view]
            for i, entry in enumerate(entries):
                x, y = (i % 4) * 480, top + 36 + (i // 4) * 318
                image = Image.open(ART / entry['path']).convert('RGB').resize((480, 300), Image.Resampling.LANCZOS)
                canvas.paste(image, (x, y))
                draw.text((x + 12, y + 301), f"{entry['time']:.2f} s / key {entry['frame']}", font=font(16), fill='#c4d3e4')
        canvas.save(ART / f'{clip}.contact.png')


def traces(report):
    groups = ['body', 'neck/head', 'jaw', 'wing shoulder l', 'wing shoulder r', 'tail',
              'wing distal l', 'wing distal r', 'hind limbs', 'forelimbs', 'hind toes', 'fore toes']
    for clip, rows in report['animations'].items():
        canvas = Image.new('RGB', (1800, 1640), '#f5f7fa')
        draw = ImageDraw.Draw(canvas)
        draw.text((24, 15), clip + ' | offset angle from rest (degrees)', font=font(27), fill='#182539')
        draw.text((24, 51), 'Each line is one joint. Norms hide rotation direction; both mirrored clips have matching angle traces.',
                  font=font(18), fill='#4c5b6c')
        for panel, group in enumerate(groups):
            members = [r for r in rows if r['group'] == group]
            x, y = (panel % 3) * 600, 90 + (panel // 3) * 385
            draw.text((x + 20, y), group, font=font(23), fill='#182539')
            limit = members[0]['limit_deg']
            peak = max(r['peak_deg'] for r in members)
            maximum = max(2, math.ceil(peak * 1.12))
            draw.text((x + 20, y + 29), f'Limit {limit} deg; peak {peak:.2f} deg', font=font(16), fill='#526275')
            left, top, width, height = x + 48, y + 62, 525, 200
            for k in range(5):
                gy = top + height * k / 4
                draw.line((left, gy, left + width, gy), fill='#d7dfe8', width=1)
                draw.text((x + 9, gy - 8), f'{maximum * (1 - k/4):.1f}', font=font(13), fill='#627286')
            for k in range(5):
                gx = left + width * k / 4
                draw.line((gx, top, gx, top + height), fill='#e3e8ef', width=1)
                draw.text((gx - 8, top + height + 6), f'{0.6*k:.1f}', font=font(13), fill='#627286')
            for j, row in enumerate(members):
                points = [(left + width * k / 72, top + height * (1 - angle / maximum))
                          for k, angle in enumerate(row['angles_deg'])]
                color = COLORS[j % len(COLORS)]
                draw.line(points, fill=color, width=2)
                lx, ly = x + 20 + (j % 2) * 290, top + height + 32 + (j // 2) * 20
                draw.line((lx, ly + 8, lx + 18, ly + 8), fill=color, width=3)
                draw.text((lx + 24, ly), row['joint'], font=font(14), fill='#25364b')
        canvas.save(ART / f'{clip}.traces.png')


def main():
    sheets(json.loads((ART / 'preview-manifest.json').read_text()))
    traces(json.loads((ART / 'checks.json').read_text()))
    print('Wrote both contact sheets and trace charts.')


if __name__ == '__main__':
    main()
