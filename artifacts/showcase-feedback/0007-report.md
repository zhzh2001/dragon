# Feedback, 2026-11-15 11:32:12

low priority shadow issues

![the frame with its marks](marked.bmp)

The frame with nothing drawn on it is `frame.bmp`.

## Marks

| # | Where | x | y | Note |
|---|---|---|---|---|
| 1 | centre | 0.476 | 0.482 | self shadow aliasing |
| 2 | bottom left | 0.162 | 0.791 | look at the bending! |

x and y are fractions of the frame from its top left corner.

## State

- model: assets/embercrest-hero.glb
- studio: scenario 1 at 70.36 s
- dragon position: 386.34, 171.93, -2102.61
- flight: 33.4 m/s, climbing 0.0 m/s, 1.0 g, 144.9 m above the ground
- camera: 378.36, 172.62, -2110.89 looking at 388.31, 176.09, -2093.88, free camera
- frame: 14903, at 137.44 s
- renderer: modern tier, 1920x1080
- combat: on
- camera from the dragon: 224 degrees round, 11.5 m away, 3 degrees up (--inspect)

## Reproduce

```sh
./build/dragon --model assets/embercrest-hero.glb --studio 1 --inspect 224 11.5 3 --headless --hide-ui --frames 4222 --screenshot /tmp/moment.bmp
```
