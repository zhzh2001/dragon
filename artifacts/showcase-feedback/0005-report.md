# Feedback, 2026-11-15 10:24:45

don't like the new folding, refer online/other games for more natural leg postures in flight

![the frame with its marks](marked.bmp)

The frame with nothing drawn on it is `frame.bmp`.

## Marks

| # | Where | x | y | Note |
|---|---|---|---|---|
| 1 | centre left | 0.304 | 0.527 | unnatural folding |
| 2 | bottom centre | 0.516 | 0.668 | looks like standing in ths sky |

x and y are fractions of the frame from its top left corner.

## State

- model: assets/embercrest-hero.glb
- studio: scenario 1 at 10.18 s
- dragon position: 537.87, 186.44, -2305.78
- flight: 33.4 m/s, climbing 0.0 m/s, 1.0 g, 140.9 m above the ground
- camera: 529.68, 185.84, -2311.10 looking at 546.79, 188.42, -2301.08, free camera
- frame: 6931, at 28.99 s
- renderer: modern tier, 1920x1080
- combat: on
- camera from the dragon: 237 degrees round, 9.8 m away, -4 degrees up (--inspect)

## Reproduce

```sh
./build/dragon --model assets/embercrest-hero.glb --studio 1 --inspect 237 9.8 -4 --headless --hide-ui --frames 611 --screenshot /tmp/moment.bmp
```
