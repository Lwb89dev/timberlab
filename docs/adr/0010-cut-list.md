# ADR 0010 — Cut list from the model, drawing from the mesh

## Status

Accepted for the cut list, the bill of materials, the orthographic tessellation wires (front, top, right, one sheet, DXF R12, and PDF), and one front file that states the model's overall width and height. Hidden-line projection is not accepted because it does not exist.

## Context

A shop needs the size of each piece and a count of like pieces. The model already stores finished nanometres. Rough stock is that size plus an allowance the user chooses. A picture of the solid is not the same document: the viewport mesh is a tessellation, and hidden-line projection is a different algorithm.

## Decision

`cutList` walks the parts. A board or a box reports width, thickness, and height. A cylinder reports the diameter as both width and thickness, and the height. Rough size is finished size plus the allowance on each of those three numbers. A negative allowance, an overflow, or a rough edge above 100 m is `parameter.range`. The bill of materials groups material, role, shape, and finished size, and counts the group.

CSV is the cut list, in millimetres printed from the nanometres. JSON stores both lists as integer nanometres, plus the allowance. The File menu writes them with allowance zero and says that rough size equals finished size.

The same mesh is projected three ways. Front drops Y: horizontal is X, up is Z. Top drops Z: horizontal is X, up is Y. Right drops X: horizontal is Y, up is Z. Each SVG has a 1 mm margin. For a 100 × 18 × 200 mm board the view boxes are 102 × 202, 102 × 20, and 20 × 202. The sheet places front, then right, then top, with a 10 mm gap, so that board's sheet is 132 × 232. `wireDxf` is DXF R12 `LINE` entities in millimetres, layers `FRONT`, `TOP`, and `RIGHT`. `wirePdf` is a PDF 1.4 page with those edges and one line of text that names the file a tessellation wire. Those files are not dimensioned drawings.

`dimensionedFrontSvg` keeps that front wire and adds two dimension lines. The text is the part's width and height, formatted from the stored nanometres. For the 100 × 18 × 200 mm board the texts are `100 mm` and `200 mm`, and the width line runs from 1 mm to 101 mm in the SVG, which is the model width. A dado does not change those two numbers. A cylinder is refused. This is not a feature dimension and not hidden-line removal.

## Consequences

- The allowance is an argument. The menu does not ask for one.
- Two parts group in the bill of materials only when material, role, shape, and finished size all match.
- TKHLR stays unused. The overall front dimensions are not a hidden-line view.
- The File menu writes these files. The menu items were not clicked in a window. The strings and the model functions are what the tests cover.
