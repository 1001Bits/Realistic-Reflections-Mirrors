# Test NIFs

## Plain (unprepared)

| File | Use |
|---|---|
| `mirror01.nif` … `mirror05.nif` | Shipped frames. Path A: assign `MOS_MirrorSurface` to `TrueMirror:0`. Path B: click that pane in Mirror Creator. |
| `simple-pane.nif` | One quad. Fastest Path B check. |
| `overhaul-cabinet.nif` | Furniture-style mesh with a wood body and a separate pane. |

## Prepared (Path B output)

These already contain `MOSReflectiveSurface:0` plus `MOSMirrorSurface` extra
data. Point a STAT/FURN/ACTI model at the prepared file. Do not also assign
`MOS_MirrorSurface`.
