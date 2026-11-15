# Feedback, 2026-11-15 11:34:27

(no summary)

![the frame with its marks](marked.bmp)

The frame with nothing drawn on it is `frame.bmp`.

## Marks

| # | Where | x | y | Note |
|---|---|---|---|---|
| 1 | centre | 0.444 | 0.411 | vegetation quality can be improved |

x and y are fractions of the frame from its top left corner.

## State

- model: assets/embercrest-hero.glb
- studio: scenario 9 at 44.77 s
- dragon position: 386.34, 30.01, -2102.61
- flight: 0.0 m/s, climbing 0.0 m/s, 1.0 g, 0.0 m above the ground, grounded
- camera: 364.56, 49.75, -2109.75 looking at 345.39, 44.18, -2111.03, free camera
- frame: 21389, at 191.92 s
- renderer: modern tier, 1920x1080
- combat: on
- camera from the dragon: 252 degrees round, 30.2 m away, 41 degrees up (--inspect)

## Reproduce

```sh
./build/dragon --model assets/embercrest-hero.glb --studio 9 --inspect 252 30.2 41 --headless --hide-ui --frames 2686 --screenshot /tmp/moment.bmp
```
