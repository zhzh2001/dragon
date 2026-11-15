# Feedback, 2026-11-14 23:12:09

(no summary)

![the frame with its marks](marked.bmp)

The frame with nothing drawn on it is `frame.bmp`.

## Marks

| # | Where | x | y | Note |
|---|---|---|---|---|
| 1 | centre | 0.444 | 0.584 | low res texture and geometry |
| 2 | top left | 0.181 | 0.253 | this looks better |
| 3 | top centre | 0.526 | 0.199 | wings are worse |

x and y are fractions of the frame from its top left corner.

## State

- model: assets/embercrest-hero.glb
- studio: scenario 0 at 96.54 s
- dragon position: -204.64, 37.01, -1310.21
- flight: 0.0 m/s, climbing 0.0 m/s, 0.0 g, 3.0 m above the ground, grounded
- camera: 603.73, 192.08, -2404.38 looking at 622.75, 192.94, -2398.27, free camera
- frame: 34315, at 243.06 s
- renderer: modern tier, 1920x1080
- combat: on

## Reproduce

```sh
./build/dragon --model assets/embercrest-hero.glb --studio 0 --cam 603.73,192.08,-2404.38,622.75,192.94,-2398.27
```
