# TimberLab material textures

This directory contains two 512 × 512 photographic-style maps for each of the
57 catalog materials:

- `*-face.jpg` is the longitudinal face grain, veneer face, or manufactured
  panel surface.
- `*-end.jpg` is true transverse end grain for solid timber and the exposed
  layered/composite edge for sheet goods.

## Research basis

Every catalog entry was researched by its scientific name (or by its product
construction for engineered materials) before generation. The visual review
used wood-anatomy and species references including:

- USDA Forest Products Laboratory, *Wood Handbook—Wood as an Engineering
  Material*: https://www.fpl.fs.usda.gov/documnts/fplgtr/fplgtr282/fpl_gtr282.pdf
- Oxford University Herbaria Xylarium: https://herbaria.plants.ox.ac.uk/bol/oxford/Xylarium
- Virginia Tech Dendrology wood-grain collection:
  https://dendro.cnre.vt.edu/dendrology/woodgrain.cfm
- Forest Research tree-species database:
  https://www.forestresearch.gov.uk/tools-and-resources/tree-species-database/

Commercial panel samples were additionally checked for ply count and veneer
construction, fibre density, particle scale, OSB strand geometry, melamine
coating, and bamboo lamination.

## Generation method

The images are original AI-generated bitmap assets, not copied photographs.
One generation call was made per material using a shared two-panel prompt:

> Create a square orthographic material-reference atlas for [material and
> scientific name]. The top half is a straight-on longitudinal face; the
> bottom half is a straight-on transverse end grain (or manufactured edge).
> Match the real material's colour, pore structure, rays, growth rings and
> figure. Flat neutral diffuse light, dry unfinished surface, sharp macro
> detail, edge-to-edge, no perspective, objects, bark, text, watermark,
> illustration or CGI look.

Species-specific anatomy and colour notes were added to that template. Each
atlas was centre-cropped into independent face and end maps, resized with a
high-quality filter, and visually reviewed in separate contact sheets.

These maps are intended for realistic material identification and viewport
communication. They are not calibrated colour measurements, microscopic
anatomy plates, or a substitute for grading a physical board; natural wood
varies by tree, cut, moisture, ageing, and finish.
