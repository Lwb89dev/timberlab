# Material database

The catalog is JSON, loaded at startup. Species are not hardcoded into QML.

`scripts/seed_data.py` is the editable source. It writes `resources/data/materials.json` and `resources/data/presets.json`.

## Species

46 hardwood and softwood rows, each with:

- stable `id` (`european-oak`, `black-walnut`, …)
- `scientificName` as genus and species
- `commonNames.en` and `commonNames.it`
- `category`: `hardwood` or `softwood`
- `visualHex`: a stylized display color, not a measured reflectance
- `grain` when the class is standard (`ring-porous`, `diffuse-porous`, `semi-ring-porous`, `softwood`). Empty when this record does not state one
- `densityKgM3` and `jankaN`: null. No number is invented
- `source`: a note that numeric properties wait for a citation
- `identityAmbiguous`: true for purpleheart (`Peltogyne spp.`), ebony (`Diospyros spp.`), and meranti (`Shorea spp.`)

## Sheet goods

`category` is `engineered`. Scientific name is empty. Included: birch, poplar, softwood, hardwood, and marine plywood, MDF, HDF, particleboard, white melamine-faced particleboard, OSB, and engineered bamboo.

## Presets

Thickness groups, in millimetres, as starting sizes rather than a supplier guarantee:

- plywood: 3, 4, 6, 9, 12, 15, 18, 21, 24
- MDF and particleboard: 3, 6, 8, 10, 12, 16, 18, 19, 22, 25
- solid wood: 15, 18, 20, 25, 30, 40, 50
- battens: 20×20, 30×30, 40×40, 30×50, 40×60, 50×50. Applying one sets the thickness (Y) to the first number. Set the width yourself.
- imperial thickness presets from 1/8 inch through 4 inch, stored as exact nanometres

Lumber rows keep three sizes:

- `roughThicknessNm` is the nominal thickness named by the trade designation
- `surfacedThicknessNm` and `surfacedWidthNm` are typical dressed sizes
- quarter designations (4/4 through 12/4) have surfaced width 0, because the designation does not imply a width

A 2×4 is nominal 2 inch thick and surfaced 1 1/2 by 3 1/2 inch. The nominal value is never copied into the part as if it were the finished size. The user still edits the actual dimension.

User-authored species and supplier overrides are not in this baseline. The shipped file is the whole catalog.
