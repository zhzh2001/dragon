# Feedback, 2026-11-14 23:08:47

(no summary)

![the frame with its marks](marked.bmp)

The frame with nothing drawn on it is `frame.bmp`.

## Marks

| # | Where | x | y | Note |
|---|---|---|---|---|
| 1 | centre left | 0.327 | 0.544 | unnatural joint |

x and y are fractions of the frame from its top left corner.

## State

- model: assets/embercrest-hero.glb
- studio: scenario 1 at 18.31 s
- dragon position: -204.64, 37.01, -1310.21
- flight: 0.0 m/s, climbing 0.0 m/s, 0.0 g, 3.0 m above the ground, grounded
- camera: 601.23, 191.35, -2402.24 looking at 621.19, 192.63, -2402.31, free camera
- frame: 17939, at 117.49 s
- renderer: modern tier, 1920x1080
- combat: on

## Reproduce

```sh
./build/dragon --model assets/embercrest-hero.glb --studio 1 --cam 601.23,191.35,-2402.24,621.19,192.63,-2402.31
```
