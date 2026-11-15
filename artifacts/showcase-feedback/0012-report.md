# Feedback, 2026-11-15 11:36:49

(no summary)

![the frame with its marks](marked.bmp)

The frame with nothing drawn on it is `frame.bmp`.

## Marks

| # | Where | x | y | Note |
|---|---|---|---|---|
| 1 | centre | 0.449 | 0.474 | mountain shapes seem weird |

x and y are fractions of the frame from its top left corner.

## State

- model: assets/embercrest-hero.glb
- studio: scenario 9 at 96.76 s
- dragon position: 386.34, 30.01, -2102.61
- flight: 0.0 m/s, climbing 0.0 m/s, 1.0 g, 0.0 m above the ground, grounded
- camera: 333.36, 98.76, -2119.04 looking at 346.80, 104.34, -2132.76, free camera
- frame: 29765, at 243.91 s
- renderer: modern tier, 1920x1080
- combat: on
- camera from the dragon: 253 degrees round, 88.3 m away, 51 degrees up (--inspect)

## Reproduce

```sh
./build/dragon --model assets/embercrest-hero.glb --studio 9 --inspect 253 88.3 51 --headless --hide-ui --frames 5806 --screenshot /tmp/moment.bmp
```
