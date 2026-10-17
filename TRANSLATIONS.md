# Translations

English is the source language of the QML and of `tr()` in the controller. Italian (`it`) is a finished catalog: `translations/timberlab_it.ts`. The build compiles it and embeds `:/i18n/timberlab_it.qm`. On startup the process loads `timberlab_<locale>` and then `timberlab_<language>`. An Italian locale gets Italian. Any other locale stays in English.

French, German, Spanish, simplified Chinese, Japanese, and Russian are not translated. They are not advertised as available. Further EU languages are the same: add a catalog only when the strings are actually translated.

## Rules

- Stable identifiers (`board`, `joint.rabbet`, species ids) do not change with the language.
- Stored nanometres do not change with the language.
- Model names typed by the user, including the English names of the demo table, are data. They are not passed through `tr()`.
- Woodworking terms should use the established local word. The UI says “Tavola” for a board in Italian, not a mechanical gloss.
- Rabbet and rebate are one feature. The identifier will be `joint.rabbet`. The label follows the language.
- CJK will need a font that covers the UI. That has not been tested, because those catalogs do not exist.

## Glossary for the strings that exist

| Identifier | English | Italian |
|---|---|---|
| component.board | Board | Tavola |
| component.box | Box | Scatola |
| component.cylinder | Cylinder | Cilindro |
| view.top | Top | Alto |
| view.front | Front | Fronte |
| view.right | Right | Destra |
| axis.width | Width (X) | Larghezza (X) |
| axis.thickness | Thickness (Y) | Spessore (Y) |
| axis.height | Height (Z) | Altezza (Z) |

Part type keys in the file remain `board`, `box`, and `cylinder`. The parts list translates them for display.

## Length text

The parser accepts either `.` or `,` as the decimal mark. The formatter currently writes `.` so a displayed value parses the same way in every locale. It does not yet follow the locale's decimal mark on output.
