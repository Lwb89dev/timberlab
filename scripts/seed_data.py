#!/usr/bin/env python3
"""Write the shipped material catalog and dimension presets. Run from the repo root."""

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (
    "Botanical name is the species identity used by this record. "
    "Density and Janka hardness are omitted until a cited measurement is attached. "
    "visualHex is a stylized display color, not a measured reflectance."
)
AMBIGUOUS = (
    "This commercial name covers more than one species. "
    "It is not a single botanical identity."
)

# id, category, scientific, hex, en, it, grain, ambiguous, notes
SPECIES = [
    ("european-oak", "hardwood", "Quercus robur", "#C4A36A", "European oak", "Rovere europeo", "ring-porous", False, ""),
    ("sessile-oak", "hardwood", "Quercus petraea", "#B8956A", "Sessile oak", "Rovere sessile", "ring-porous", False, ""),
    ("white-oak", "hardwood", "Quercus alba", "#D7C4A3", "American white oak", "Rovere bianco americano", "ring-porous", False, ""),
    ("red-oak", "hardwood", "Quercus rubra", "#C47A52", "Northern red oak", "Quercia rossa", "ring-porous", False, ""),
    ("european-walnut", "hardwood", "Juglans regia", "#6B4A32", "European walnut", "Noce europeo", "semi-ring-porous", False, ""),
    ("black-walnut", "hardwood", "Juglans nigra", "#3E2A1E", "American black walnut", "Noce nero americano", "semi-ring-porous", False, ""),
    ("hard-maple", "hardwood", "Acer saccharum", "#E6D3B0", "Hard maple", "Acero duro", "diffuse-porous", False, ""),
    ("red-maple", "hardwood", "Acer rubrum", "#D8B48A", "Red maple", "Acero rosso", "diffuse-porous", False, ""),
    ("european-beech", "hardwood", "Fagus sylvatica", "#D2A679", "European beech", "Faggio europeo", "diffuse-porous", False, ""),
    ("european-ash", "hardwood", "Fraxinus excelsior", "#E4D2B0", "European ash", "Frassino europeo", "ring-porous", False, ""),
    ("black-cherry", "hardwood", "Prunus serotina", "#8C4A3A", "Black cherry", "Ciliegio nero", "diffuse-porous", False, ""),
    ("sweet-cherry", "hardwood", "Prunus avium", "#A85A48", "Sweet cherry", "Ciliegio selvatico", "diffuse-porous", False, ""),
    ("silver-birch", "hardwood", "Betula pendula", "#F0E2C8", "Silver birch", "Betulla bianca", "diffuse-porous", False, ""),
    ("black-poplar", "hardwood", "Populus nigra", "#E7D7B8", "Black poplar", "Pioppo nero", "diffuse-porous", False, ""),
    ("black-alder", "hardwood", "Alnus glutinosa", "#C9865A", "Black alder", "Ontano nero", "diffuse-porous", False, ""),
    ("wych-elm", "hardwood", "Ulmus glabra", "#B08A5A", "Wych elm", "Olmo montano", "ring-porous", False, ""),
    ("sweet-chestnut", "hardwood", "Castanea sativa", "#A87448", "Sweet chestnut", "Castagno europeo", "ring-porous", False, ""),
    ("black-locust", "hardwood", "Robinia pseudoacacia", "#C6A15A", "Black locust", "Robinia", "ring-porous", False, ""),
    ("teak", "hardwood", "Tectona grandis", "#B8864A", "Teak", "Teak", "ring-porous", False, ""),
    ("sapele", "hardwood", "Entandrophragma cylindricum", "#A85B3C", "Sapele", "Sapelli", "", False, ""),
    ("iroko", "hardwood", "Milicia excelsa", "#C4923E", "Iroko", "Iroko", "", False, ""),
    ("wenge", "hardwood", "Millettia laurentii", "#2C2118", "Wenge", "Wengé", "", False, ""),
    ("zebrano", "hardwood", "Microberlinia brazzavillensis", "#C4A15A", "Zebrano", "Zebrano", "", False, ""),
    ("rubberwood", "hardwood", "Hevea brasiliensis", "#E2C89A", "Rubberwood", "Legno di gomma", "", False, ""),
    ("shagbark-hickory", "hardwood", "Carya ovata", "#C9A06A", "Shagbark hickory", "Hickory", "ring-porous", False, ""),
    ("sycamore-maple", "hardwood", "Acer pseudoplatanus", "#E8D8BE", "Sycamore maple", "Acero di monte", "diffuse-porous", False, ""),
    ("american-sycamore", "hardwood", "Platanus occidentalis", "#D9C7A4", "American sycamore", "Platano americano", "diffuse-porous", False, ""),
    ("hornbeam", "hardwood", "Carpinus betulus", "#E0CBA8", "Hornbeam", "Carpino bianco", "diffuse-porous", False, ""),
    ("okoume", "hardwood", "Aucoumea klaineana", "#E4B07A", "Okoumé", "Okoumé", "", False, ""),
    ("african-padauk", "hardwood", "Pterocarpus soyauxii", "#C2412D", "African padauk", "Padouk africano", "", False, ""),
    ("honduran-mahogany", "hardwood", "Swietenia macrophylla", "#8A3E2A", "Honduran mahogany", "Mogano dell'Honduras", "diffuse-porous", False, ""),
    ("european-olive", "hardwood", "Olea europaea", "#C2B48A", "European olive", "Olivo", "", False, ""),
    ("norway-maple", "hardwood", "Acer platanoides", "#E7D2A4", "Norway maple", "Acero riccio", "diffuse-porous", False, ""),
    ("yellow-poplar", "hardwood", "Liriodendron tulipifera", "#E3C27A", "Yellow poplar", "Pioppo tulipifero", "diffuse-porous", False, ""),
    ("purpleheart", "hardwood", "Peltogyne spp.", "#6A3A78", "Purpleheart", "Amaranto", "", True, AMBIGUOUS),
    ("ebony", "hardwood", "Diospyros spp.", "#1A1714", "Ebony", "Ebano", "", True, AMBIGUOUS),
    ("meranti", "hardwood", "Shorea spp.", "#C46A4A", "Meranti", "Meranti", "", True, AMBIGUOUS),
    ("scots-pine", "softwood", "Pinus sylvestris", "#E6C98A", "Scots pine", "Pino silvestre", "softwood", False, ""),
    ("norway-spruce", "softwood", "Picea abies", "#F3E6C4", "Norway spruce", "Abete rosso", "softwood", False, ""),
    ("douglas-fir", "softwood", "Pseudotsuga menziesii", "#C9A56B", "Douglas fir", "Abete di Douglas", "softwood", False, ""),
    ("european-larch", "softwood", "Larix decidua", "#D7B07A", "European larch", "Larice europeo", "softwood", False, ""),
    ("western-red-cedar", "softwood", "Thuja plicata", "#C46A4E", "Western red cedar", "Cedro rosso occidentale", "softwood", False, ""),
    ("silver-fir", "softwood", "Abies alba", "#F6EBD2", "European silver fir", "Abete bianco", "softwood", False, ""),
    ("eastern-white-pine", "softwood", "Pinus strobus", "#F2E3C0", "Eastern white pine", "Pino strobo", "softwood", False, ""),
    ("european-yew", "softwood", "Taxus baccata", "#8C5A3C", "European yew", "Tasso", "softwood", False, ""),
    ("mediterranean-cypress", "softwood", "Cupressus sempervirens", "#C8B48A", "Mediterranean cypress", "Cipresso", "softwood", False, ""),
]

ENGINEERED = [
    ("birch-plywood", "#E7D3A1", "Birch plywood", "Multistrato di betulla"),
    ("poplar-plywood", "#EAD7B4", "Poplar plywood", "Multistrato di pioppo"),
    ("softwood-plywood", "#E4C98A", "Softwood plywood", "Multistrato di conifera"),
    ("hardwood-plywood", "#C9A36E", "Hardwood plywood", "Multistrato di latifoglia"),
    ("marine-plywood", "#D7B56A", "Marine plywood", "Compensato marino"),
    ("mdf", "#C4A888", "MDF", "MDF"),
    ("hdf", "#B09070", "HDF", "HDF"),
    ("particleboard", "#D2BE96", "Particleboard", "Truciolare"),
    ("melamine-white", "#F4F1EA", "White melamine-faced particleboard", "Truciolare melaminico bianco"),
    ("osb", "#D8C07A", "OSB", "OSB"),
    ("engineered-bamboo", "#C6B05A", "Engineered bamboo", "Bambù ingegnerizzato"),
]

entries = []
for item in SPECIES:
    ident, category, scientific, color, english, italian, grain, ambiguous, notes = item
    entries.append(
        {
            "id": ident,
            "category": category,
            "scientificName": scientific,
            "visualHex": color,
            "identityAmbiguous": ambiguous,
            "commonNames": {"en": english, "it": italian},
            "grain": grain,
            "densityKgM3": None,
            "jankaN": None,
            "source": SOURCE,
            "notes": notes,
        }
    )

for ident, color, english, italian in ENGINEERED:
    entries.append(
        {
            "id": ident,
            "category": "engineered",
            "scientificName": "",
            "visualHex": color,
            "identityAmbiguous": False,
            "commonNames": {"en": english, "it": italian},
            "grain": "",
            "densityKgM3": None,
            "jankaN": None,
            "source": "Sheet good. Composition and coating are described by the common name, not by a single tree species.",
            "notes": "Stock thicknesses live in the preset library. They are starting sizes, not a supplier guarantee.",
        }
    )

materials = {"schema": "timberlab.materials", "schemaVersion": 1, "entries": entries}

NM = 1_000_000
INCH = 25_400_000


def mm(value):
    return value * NM


thicknesses = []
for value in (3, 4, 6, 9, 12, 15, 18, 21, 24):
    thicknesses.append({"id": f"ply-{value}", "group": "plywood", "label": f"Plywood {value} mm", "nm": mm(value)})
for value in (3, 6, 8, 10, 12, 16, 18, 19, 22, 25):
    thicknesses.append({"id": f"mdf-{value}", "group": "mdf", "label": f"MDF {value} mm", "nm": mm(value)})
    thicknesses.append(
        {"id": f"particle-{value}", "group": "particleboard", "label": f"Particleboard {value} mm", "nm": mm(value)}
    )
for value in (15, 18, 20, 25, 30, 40, 50):
    thicknesses.append(
        {"id": f"solid-{value}", "group": "solid-wood", "label": f"Solid wood {value} mm", "nm": mm(value)}
    )
for section in ("20x20", "30x30", "40x40", "30x50", "40x60", "50x50"):
    first = int(section.split("x")[0])
    thicknesses.append(
        {
            "id": f"batten-{section}",
            "group": "batten",
            "label": f"Batten {section.replace('x', '×')} mm",
            "nm": mm(first),
        }
    )

imperial = [
    ("1/8", 1, 8),
    ("1/4", 1, 4),
    ("3/8", 3, 8),
    ("1/2", 1, 2),
    ("5/8", 5, 8),
    ("3/4", 3, 4),
    ("1", 1, 1),
    ("1-1-4", 5, 4),
    ("1-1-2", 3, 2),
    ("2", 2, 1),
    ("3", 3, 1),
    ("4", 4, 1),
]
labels = {
    "1/8": '1/8"',
    "1/4": '1/4"',
    "3/8": '3/8"',
    "1/2": '1/2"',
    "5/8": '5/8"',
    "3/4": '3/4"',
    "1": '1"',
    "1-1-4": '1 1/4"',
    "1-1-2": '1 1/2"',
    "2": '2"',
    "3": '3"',
    "4": '4"',
}
for key, numerator, denominator in imperial:
    thicknesses.append(
        {
            "id": f"imp-{key.replace('/', '-')}",
            "group": "imperial",
            "label": labels[key],
            "nm": INCH * numerator // denominator,
        }
    )

# Quarter-sawn designations. Surfaced thickness is the common US S4S starting size.
# Width 0 means the designation does not imply a width.
quarters = [
    ("4-4", "4/4", 4, 13),
    ("5-4", "5/4", 5, 17),
    ("6-4", "6/4", 6, 21),
    ("8-4", "8/4", 8, 28),
    ("10-4", "10/4", 10, 36),
    ("12-4", "12/4", 12, 44),
]
lumber = []
for ident, nominal, rough_quarters, surfaced_sixteenths in quarters:
    lumber.append(
        {
            "id": f"quarter-{ident}",
            "nominal": nominal,
            "roughThicknessNm": INCH * rough_quarters // 4,
            "surfacedThicknessNm": INCH * surfaced_sixteenths // 16,
            "surfacedWidthNm": 0,
            "note": "Nominal quarter thickness. Surfaced thickness is a typical North American S4S starting size, not a finished part. Width is not defined. Actual size stays editable.",
        }
    )

dimensional = [
    ("1x2", "1×2", 1, 0.75, 1.5),
    ("1x4", "1×4", 1, 0.75, 3.5),
    ("2x2", "2×2", 2, 1.5, 1.5),
    ("2x4", "2×4", 2, 1.5, 3.5),
    ("2x6", "2×6", 2, 1.5, 5.5),
    ("4x4", "4×4", 4, 3.5, 3.5),
]
for ident, nominal, rough_in, thick_in, wide_in in dimensional:
    lumber.append(
        {
            "id": f"dim-{ident}",
            "nominal": nominal,
            "roughThicknessNm": int(round(INCH * rough_in)),
            "surfacedThicknessNm": int(round(INCH * thick_in)),
            "surfacedWidthNm": int(round(INCH * wide_in)),
            "note": "Nominal is the trade name, not the physical size. Surfaced sizes are typical North American dressed lumber. Supplier stock can differ. Actual size stays editable.",
        }
    )

presets = {"schema": "timberlab.presets", "schemaVersion": 1, "thicknesses": thicknesses, "lumber": lumber}

out = ROOT / "resources" / "data"
out.mkdir(parents=True, exist_ok=True)
(out / "materials.json").write_text(json.dumps(materials, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
(out / "presets.json").write_text(json.dumps(presets, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
print(f"species {sum(1 for e in entries if e['category'] in ('hardwood', 'softwood'))}")
print(f"entries {len(entries)} thicknesses {len(thicknesses)} lumber {len(lumber)}")
