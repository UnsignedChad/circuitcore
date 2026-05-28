# Contributors

People who reported bugs, suggested features, or sent fixes outside the
core author. Listed roughly in order of first contribution.

## TheHexaCube ([@TheHexaCube](https://github.com/TheHexaCube))

Bug reports + reproducers that drove the following fixes:

- PI static IR drop: solver was rejecting auto-balanced pad current lists
  with "per-node currents do not sum to zero" because multiple pads
  sharing a pin name (every footprint has a pin "1") collapsed into the
  same string-keyed map entry, dropping all-but-the-last current before
  the KCL check. Fixed by switching `pad_currents` to be keyed by pad
  index. (commit `a790f75`)
- Filleted / arc-routed traces were silently dropped from the canvas
  because the parser only handled the `(segment ...)` primitive, not
  `(arc ...)`. Fixed by tessellating each arc into a polyline of straight
  segments at parse time, which then flow through the existing renderer
  + SI / PI analyses for free. (commit `c44968b`)
- PTH vias visually read as "disconnected between top and bottom"
  because the via mesher was punching a dark drill disk through the
  pad. PTHs are plated, so the top-down view should show solid copper.
  Fixed by dropping the drill punch. (commit `b37c19b`)
- No way to hide silkscreen + soldermask overlays on dense boards.
  Added a View menu toggle (Ctrl+Shift+L) that hides silk / mask /
  courtyard / silk text in one shot, propagated to every `PcbCanvas`
  in the studio tab hierarchy. (commit `5b89b4c`)
- 3D Mp view ignored `pd.rotation` on rectangular pads, so QFP / QFN
  packages placed at a non-zero rotation had their side pads sticking
  out axis-aligned instead of facing the landing pattern. Fixed by
  adding `append_oriented_box` and routing rotated pads through it.
  (commit `45aa846`)
