#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace timberlab::units {

// Canonical length is an integer number of nanometres.
// 1 inch = 25.4 mm = 25_400_000 nm exactly, so binary inch fractions
// down to 1/64" are exact. Display rounding never writes back into this value.
inline constexpr std::int64_t kNmPerMm = 1'000'000;
inline constexpr std::int64_t kNmPerCm = 10'000'000;
inline constexpr std::int64_t kNmPerMetre = 1'000'000'000;
inline constexpr std::int64_t kNmPerInch = 25'400'000;
inline constexpr std::int64_t kNmPerFoot = kNmPerInch * 12;

// A single part larger than this is rejected. 100 m.
inline constexpr std::int64_t kMaxPartNm = 100'000'000'000LL;

enum class LengthUnit { Millimetre, Centimetre, Metre, Inch, Foot };

struct ParseResult {
  bool ok = false;
  std::int64_t nm = 0;
  std::string error;
};

// `defaultUnit` applies when the text has no unit suffix.
// Both '.' and ',' are decimal separators. A space is not a thousands separator.
// Fractions and feet/inches: 3/4", 1 1/2", 3' 6 1/4".
ParseResult parseLength(std::string_view text, LengthUnit defaultUnit);

// fractionDenom is 2, 4, 8, 16, 32 or 64. Imperial values that are not an
// exact fraction at that denominator are printed as decimals, not rounded.
std::string formatLength(std::int64_t nm, LengthUnit unit, int fractionDenom);

bool lengthUnitFromKey(std::string_view key, LengthUnit& out);
const char* lengthUnitKey(LengthUnit unit);
bool validFractionDenom(int denom);

double toMillimetres(std::int64_t nm);

struct AngleParse {
  bool ok = false;
  double degrees = 0;
  std::string error;
};

AngleParse parseAngle(std::string_view text);
std::string formatAngle(double degrees);

}  // namespace timberlab::units
