# Feedback, 2026-11-15 11:36:19

(no summary)

![the frame with its marks](marked.bmp)

The frame with nothing drawn on it is `frame.bmp`.

## Marks

| # | Where | x | y | Note |
|---|---|---|---|---|
| 1 | bottom left | 0.321 | 0.764 | no shadow within mountain shadow looks strange? |
| 2 | top left | 0.291 | 0.128 | rocks look unnatural |

x and y are fractions of the frame from its top left corner.

## State

- model: assets/embercrest-hero.glb
- studio: scenario 9 at 89.22 s
- dragon position: 386.34, 30.01, -2102.61
- flight: 0.0 m/s, climbing 0.0 m/s, 1.0 g, 0.0 m above the ground, grounded
- camera: 333.36, 98.76, -2119.04 looking at 313.80, 94.73, -2117.97, free camera
- frame: 27817, at 236.37 s
- renderer: modern tier, 1920x1080
- combat: on
- camera from the dragon: 253 degrees round, 88.3 m away, 51 degrees up (--inspect)

## Reproduce

```sh
./build/dragon --model assets/embercrest-hero.glb --studio 9 --inspect 253 88.3 51 --headless --hide-ui --frames 5353 --screenshot /tmp/moment.bmp
```
