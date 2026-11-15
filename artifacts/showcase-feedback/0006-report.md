# Feedback, 2026-11-15 11:30:38

(no summary)

![the frame with its marks](marked.bmp)

The frame with nothing drawn on it is `frame.bmp`.

## Marks

| # | Where | x | y | Note |
|---|---|---|---|---|
| 1 | centre | 0.532 | 0.474 | unnatural bending |
| 2 | centre | 0.520 | 0.552 | geometry artifacts |
| 3 | centre | 0.480 | 0.503 | not completely fixed |

x and y are fractions of the frame from its top left corner.

## State

- model: assets/embercrest-hero.glb
- studio: scenario 1 at 32.34 s
- dragon position: 386.34, 171.93, -2102.61
- flight: 31.4 m/s, climbing 0.0 m/s, 1.0 g, 144.9 m above the ground
- camera: 381.20, 166.23, -2115.01 looking at 390.63, 176.12, -2100.41, free camera
- frame: 9872, at 99.41 s
- renderer: modern tier, 1920x1080
- combat: on
- camera from the dragon: 203 degrees round, 14.6 m away, -23 degrees up (--inspect)

## Reproduce

```sh
./build/dragon --model assets/embercrest-hero.glb --studio 1 --inspect 203 14.6 -23 --headless --hide-ui --frames 1940 --screenshot /tmp/moment.bmp
```
