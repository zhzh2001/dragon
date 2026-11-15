# Feedback, 2026-11-15 11:35:19

water distant view

![the frame with its marks](marked.bmp)

The frame with nothing drawn on it is `frame.bmp`.

## Marks

| # | Where | x | y | Note |
|---|---|---|---|---|
| 1 | centre | 0.502 | 0.483 | strange whirls? |

x and y are fractions of the frame from its top left corner.

## State

- model: assets/embercrest-hero.glb
- studio: scenario 9 at 65.99 s
- dragon position: 386.34, 30.01, -2102.61
- flight: 0.0 m/s, climbing 0.0 m/s, 1.0 g, 0.0 m above the ground, grounded
- camera: 333.36, 98.76, -2119.04 looking at 333.74, 91.06, -2100.58, free camera
- frame: 24226, at 213.14 s
- renderer: modern tier, 1920x1080
- combat: on
- camera from the dragon: 253 degrees round, 88.3 m away, 51 degrees up (--inspect)

## Reproduce

```sh
./build/dragon --model assets/embercrest-hero.glb --studio 9 --inspect 253 88.3 51 --headless --hide-ui --frames 3959 --screenshot /tmp/moment.bmp
```
