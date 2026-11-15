# Wing repair review

The pairs show the original model on the left and the repaired model on the right.
Each crop retains the source pixels. No image is scaled.
`render_views.py` records the seven requested views at 1920 by 1080.
`pair_views.py` records each crop rectangle.

The renderer is `~/src/game-claude/build/dragon`.
Each run loads this worktree's model by its absolute path.
This task does not build or modify the engine.

| Pair | Scenario | Inspection camera | Frames |
|---|---|---|---:|
| `pair-feedback-root.jpg` | Studio 0 | 275 10.5 3 | 483 |
| `pair-feedback-wrist.jpg` | Studio 1 | 222 11.5 7 | 2185 |
| `pair-wrist-bind.jpg` | Bind pose | 200 9 25 | 20 |
| `pair-wrist-glide.jpg` | Studio 0 | 200 9 25 | 60 |
| `pair-root-glide.jpg` | Studio 0 | 300 9 -10 | 120 |
| `pair-root-flap.jpg` | Studio 1 | 300 9 -10 | 330 |
| `pair-standing.jpg` | Studio 9 | 60 11 3 | 200 |

## Engine material finding

The current engine source does not load the membrane thickness PNG.
The main checkout and this worktree use `App::mark_membranes` to derive transmission from finger weights.
The shader reads that share from the tangent's fourth component.
`shaders/skinned.hlsl` combines that share with the base colour and the sun direction.
The shader also samples the normal map.

Thus, the PNG cannot explain the original rendered paleness in this engine.
The build still repairs and checks the PNG because it is part of the asset contract.

## Visual result

The first visual pass replaced the blades but introduced broad dark patches along the wing bones.
`rejected-pass1-wrist.jpg` records that failure.
The builder now preserves existing bone-shell normals.

The second pass passed the numerical gates and removed the dark patches.
The wrist claw read as a small hooked talon in bind, glide, and the marked view.
The root attachment looked closed in glide and flap. Standing retained the folded wings and grounded feet.
However, the root's repeated fine grain looked stronger than the neighboring panel.
`rejected-pass2-root.jpg` records that failure.
The small donor tile lacked broad veins, so contrast matching exaggerated fine grain.

The third pass changes only the root maps.
All seven final pairs were opened and reviewed at their original crop size.
The thumb reads as a small curved talon in bind and glide, with its base seated on the wrist.
The root now carries continuous veins and grain. The repeated pattern is gone.
The flank edge looks closed in both rear views, and standing retains the folded wings and grounded feet.
A dark copied vein runs beside the flank edge. The flap view retains a broad lighting highlight.
Neither feature reads as a gap or a transparent sheet.

The final candidate is accepted after three visual passes.
One texture limitation remains: narrow donor margins extend nearby detail by up to 257 mm.
The panel interior retains the neighboring grain scale and passes the colour and contrast checks.
