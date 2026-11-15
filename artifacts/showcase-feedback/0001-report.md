# Feedback, 2026-11-14 23:08:24

(no summary)

![the frame with its marks](marked.bmp)

The frame with nothing drawn on it is `frame.bmp`.

## Marks

| # | Where | x | y | Note |
|---|---|---|---|---|
| 1 | centre | 0.546 | 0.525 | artifact |
| 2 | bottom centre | 0.411 | 0.688 | back stretch too much? |

x and y are fractions of the frame from its top left corner.

## State

- model: assets/embercrest-hero.glb
- studio: scenario 1 at 13.39 s
- dragon position: -204.64, 37.01, -1310.21
- flight: 0.0 m/s, climbing 0.0 m/s, 0.0 g, 3.0 m above the ground, grounded
- camera: 601.23, 191.35, -2402.24 looking at 621.19, 192.63, -2402.31, free camera
- frame: 16080, at 112.58 s
- renderer: modern tier, 1920x1080
- combat: on

## Reproduce

```sh
./build/dragon --model assets/embercrest-hero.glb --studio 1 --cam 601.23,191.35,-2402.24,621.19,192.63,-2402.31
```
