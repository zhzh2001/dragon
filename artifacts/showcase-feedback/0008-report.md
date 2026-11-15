# Feedback, 2026-11-15 11:33:44

(no summary)

![the frame with its marks](marked.bmp)

The frame with nothing drawn on it is `frame.bmp`.

## Marks

| # | Where | x | y | Note |
|---|---|---|---|---|
| 1 | centre | 0.402 | 0.644 | water artifacts |
| 2 | top left | 0.029 | 0.053 | low performance |

x and y are fractions of the frame from its top left corner.

## State

- model: assets/embercrest-hero.glb
- studio: scenario 9 at 29.70 s
- dragon position: 386.34, 30.01, -2102.61
- flight: 0.0 m/s, climbing 0.0 m/s, 1.0 g, 0.0 m above the ground, grounded
- camera: 374.80, 44.00, -2109.38 looking at 391.82, 34.83, -2104.28, free camera
- frame: 18597, at 176.85 s
- renderer: modern tier, 1920x1080
- combat: on
- camera from the dragon: 240 degrees round, 19.4 m away, 46 degrees up (--inspect)

## Reproduce

```sh
./build/dragon --model assets/embercrest-hero.glb --studio 9 --inspect 240 19.4 46 --headless --hide-ui --frames 1782 --screenshot /tmp/moment.bmp
```
