# ADR 0002 — Internal length unit

## Status

Accepted.

## Decision

Every stored length is an integer number of nanometres (`int64`). One inch is exactly 25 400 000 nm, so 1/2, 1/4, 1/8, 1/16, 1/32, and 1/64 inch are exact. Display conversion never writes back. A value that is not an exact fraction at the chosen denominator is printed as a decimal inch with enough digits to parse back to the same nanometres.

OCCT is called with double millimetres at the boundary only. The product of a conversion uses a 128-bit intermediate so a fraction scale does not overflow `int64`. The multiply and divide are written with 64-bit limbs. On a compiler that provides `__int128`, each conversion also computes that result and refuses the parse if the two differ. MSVC has not been used to compile this tree. Windows, macOS, and Android builds are still absent on this machine.

The largest accepted part edge is 100 m.

## Consequences

- Metric and imperial projects are the same file.
- Locale decimal marks: input accepts `.` and `,`. Output currently uses `.`.
- Nominal lumber sizes are stored beside surfaced sizes and are not used as the part dimension.
