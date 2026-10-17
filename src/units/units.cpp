#include "timberlab/units/units.hpp"

#include <charconv>
#include <cmath>
#include <limits>

namespace timberlab::units {
namespace {

struct Scan {
  std::string_view s;
  std::size_t i = 0;

  void skip() {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
  }
  bool eof() const { return i >= s.size(); }
  char peek() const { return eof() ? '\0' : s[i]; }
  bool startsWith(std::string_view lit) const {
    return s.substr(i, lit.size()) == lit;
  }
  bool consume(std::string_view lit) {
    if (!startsWith(lit)) return false;
    i += lit.size();
    return true;
  }
};

struct Number {
  bool negative = false;
  std::int64_t whole = 0;
  std::int64_t frac = 0;
  int fracDigits = 0;
  bool hasFrac = false;
};

enum class UnitKind { None, Millimetre, Centimetre, Metre, Inch, Foot };

ParseResult fail(const char* code) {
  ParseResult result;
  result.error = code;
  return result;
}

bool isDigit(char c) { return c >= '0' && c <= '9'; }

std::string asciiLower(std::string_view text) {
  std::string out(text);
  for (char& c : out) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return out;
}

bool parseInt(Scan& scan, std::int64_t& out) {
  if (scan.eof() || !isDigit(scan.peek())) return false;
  std::int64_t value = 0;
  while (!scan.eof() && isDigit(scan.peek())) {
    const int digit = scan.peek() - '0';
    if (value > (std::numeric_limits<std::int64_t>::max() - digit) / 10) return false;
    value = value * 10 + digit;
    ++scan.i;
  }
  out = value;
  return true;
}

bool parseNumber(Scan& scan, Number& number) {
  scan.skip();
  if (scan.consume("+")) {
    number.negative = false;
  } else if (scan.consume("-")) {
    number.negative = true;
  }
  if (!parseInt(scan, number.whole)) return false;
  if (scan.peek() == '.' || scan.peek() == ',') {
    ++scan.i;
    number.hasFrac = true;
    const std::size_t start = scan.i;
    if (!parseInt(scan, number.frac)) return false;
    number.fracDigits = static_cast<int>(scan.i - start);
    if (number.fracDigits > 9) return false;
  }
  return true;
}

std::int64_t pow10(int digits) {
  std::int64_t value = 1;
  for (int i = 0; i < digits; ++i) value *= 10;
  return value;
}

bool mulDivRound(std::int64_t magnitude, std::int64_t factor, std::int64_t denominator, bool negative,
                 std::int64_t& out) {
  if (denominator <= 0 || magnitude < 0 || factor < 0) return false;
#if defined(__SIZEOF_INT128__)
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
  __int128 product = static_cast<__int128>(magnitude) * static_cast<__int128>(factor);
  product += denominator / 2;
  product /= denominator;
  if (product > static_cast<__int128>(std::numeric_limits<std::int64_t>::max())) return false;
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
  out = negative ? -static_cast<std::int64_t>(product) : static_cast<std::int64_t>(product);
  return true;
#else
#error "TimberLab length math needs a 128-bit integer. Add a portable path before building with this compiler."
#endif
}

bool numberToNm(const Number& number, std::int64_t unitNm, std::int64_t& out) {
  const std::int64_t scale = number.hasFrac ? pow10(number.fracDigits) : 1;
  const std::int64_t magnitude = number.whole * scale + (number.hasFrac ? number.frac : 0);
  return mulDivRound(magnitude, unitNm, scale, number.negative, out);
}

bool fractionToNm(std::int64_t numerator, std::int64_t denominator, std::int64_t unitNm, bool negative,
                  std::int64_t& out) {
  if (denominator <= 0) return false;
  return mulDivRound(numerator, unitNm, denominator, negative, out);
}

std::int64_t scaleOf(UnitKind kind, LengthUnit fallback) {
  switch (kind) {
    case UnitKind::Millimetre:
      return kNmPerMm;
    case UnitKind::Centimetre:
      return kNmPerCm;
    case UnitKind::Metre:
      return kNmPerMetre;
    case UnitKind::Inch:
      return kNmPerInch;
    case UnitKind::Foot:
      return kNmPerFoot;
    case UnitKind::None:
      break;
  }
  switch (fallback) {
    case LengthUnit::Millimetre:
      return kNmPerMm;
    case LengthUnit::Centimetre:
      return kNmPerCm;
    case LengthUnit::Metre:
      return kNmPerMetre;
    case LengthUnit::Inch:
      return kNmPerInch;
    case LengthUnit::Foot:
      return kNmPerFoot;
  }
  return kNmPerMm;
}

bool consumeWord(Scan& scan, std::string_view word) {
  if (!scan.startsWith(word)) return false;
  const std::size_t end = scan.i + word.size();
  if (end < scan.s.size()) {
    const char next = scan.s[end];
    if ((next >= 'a' && next <= 'z') || (next >= 'A' && next <= 'Z')) return false;
  }
  scan.i = end;
  return true;
}

bool consumeUnit(Scan& scan, UnitKind& kind) {
  scan.skip();
  if (consumeWord(scan, "mm")) {
    kind = UnitKind::Millimetre;
    return true;
  }
  if (consumeWord(scan, "cm")) {
    kind = UnitKind::Centimetre;
    return true;
  }
  if (consumeWord(scan, "feet") || consumeWord(scan, "foot") || consumeWord(scan, "ft")) {
    kind = UnitKind::Foot;
    return true;
  }
  if (consumeWord(scan, "inches") || consumeWord(scan, "inch") || consumeWord(scan, "in")) {
    kind = UnitKind::Inch;
    return true;
  }
  if (consumeWord(scan, "m")) {
    kind = UnitKind::Metre;
    return true;
  }
  if (scan.consume("″") || scan.consume("''") || scan.consume("\"")) {
    kind = UnitKind::Inch;
    return true;
  }
  if (scan.consume("′") || scan.consume("'")) {
    kind = UnitKind::Foot;
    return true;
  }
  return false;
}

bool looksLikeMixed(const Scan& scan) {
  Scan copy = scan;
  Number number;
  if (!parseNumber(copy, number) || number.hasFrac || number.negative) return false;
  copy.skip();
  return copy.peek() == '/';
}

bool finishAtEnd(Scan& scan) {
  scan.skip();
  return scan.eof();
}

ParseResult finishSimple(Scan& scan, const Number& number, UnitKind kind, LengthUnit fallback) {
  if (!finishAtEnd(scan)) return fail("units.trailing");
  ParseResult result;
  if (!numberToNm(number, scaleOf(kind, fallback), result.nm)) return fail("units.overflow");
  result.ok = true;
  return result;
}

ParseResult finishFraction(Scan& scan, const Number& numerator, LengthUnit fallback) {
  if (numerator.hasFrac || !scan.consume("/")) return fail("units.invalid");
  std::int64_t denominator = 0;
  if (!parseInt(scan, denominator) || denominator == 0) return fail("units.invalid");
  UnitKind kind = UnitKind::None;
  consumeUnit(scan, kind);
  if (!finishAtEnd(scan)) return fail("units.trailing");
  ParseResult result;
  if (!fractionToNm(numerator.whole, denominator, scaleOf(kind, fallback), numerator.negative, result.nm)) {
    return fail("units.overflow");
  }
  result.ok = true;
  return result;
}

ParseResult finishMixed(Scan& scan, const Number& whole, LengthUnit fallback) {
  Number numerator;
  if (!parseNumber(scan, numerator) || numerator.hasFrac || numerator.negative) return fail("units.invalid");
  scan.skip();
  if (!scan.consume("/")) return fail("units.invalid");
  std::int64_t denominator = 0;
  if (!parseInt(scan, denominator) || denominator == 0) return fail("units.invalid");
  UnitKind kind = UnitKind::None;
  consumeUnit(scan, kind);
  if (!finishAtEnd(scan)) return fail("units.trailing");

  const std::int64_t unit = scaleOf(kind, fallback);
  std::int64_t wholeNm = 0;
  std::int64_t fracNm = 0;
  Number positive = whole;
  const bool negative = whole.negative;
  positive.negative = false;
  if (!numberToNm(positive, unit, wholeNm)) return fail("units.overflow");
  if (!fractionToNm(numerator.whole, denominator, unit, false, fracNm)) return fail("units.overflow");
  ParseResult result;
  result.ok = true;
  result.nm = negative ? -(wholeNm + fracNm) : (wholeNm + fracNm);
  return result;
}

ParseResult finishFeet(Scan& scan, const Number& feet, LengthUnit fallback) {
  (void)fallback;
  scan.skip();
  const bool negative = feet.negative;
  Number positiveFeet = feet;
  positiveFeet.negative = false;
  std::int64_t total = 0;
  if (!numberToNm(positiveFeet, kNmPerFoot, total)) return fail("units.overflow");
  if (scan.eof()) {
    ParseResult result;
    result.ok = true;
    result.nm = negative ? -total : total;
    return result;
  }

  Number inches;
  if (!parseNumber(scan, inches) || inches.negative) return fail("units.invalid");
  scan.skip();
  std::int64_t inchNm = 0;
  if (scan.peek() == '/') {
    if (inches.hasFrac || !scan.consume("/")) return fail("units.invalid");
    std::int64_t denominator = 0;
    if (!parseInt(scan, denominator) || denominator == 0) return fail("units.invalid");
    if (!fractionToNm(inches.whole, denominator, kNmPerInch, false, inchNm)) return fail("units.overflow");
  } else if (looksLikeMixed(scan)) {
    if (inches.hasFrac) return fail("units.invalid");
    Number numerator;
    if (!parseNumber(scan, numerator)) return fail("units.invalid");
    scan.skip();
    if (!scan.consume("/")) return fail("units.invalid");
    std::int64_t denominator = 0;
    if (!parseInt(scan, denominator) || denominator == 0) return fail("units.invalid");
    std::int64_t wholeNm = 0;
    std::int64_t fracNm = 0;
    if (!numberToNm(inches, kNmPerInch, wholeNm)) return fail("units.overflow");
    if (!fractionToNm(numerator.whole, denominator, kNmPerInch, false, fracNm)) return fail("units.overflow");
    inchNm = wholeNm + fracNm;
  } else {
    if (!numberToNm(inches, kNmPerInch, inchNm)) return fail("units.overflow");
  }

  UnitKind trailing = UnitKind::None;
  if (consumeUnit(scan, trailing) && trailing != UnitKind::Inch) return fail("units.invalid");
  if (!finishAtEnd(scan)) return fail("units.trailing");
  total += inchNm;
  ParseResult result;
  result.ok = true;
  result.nm = negative ? -total : total;
  return result;
}

std::string trimZeros(std::string digits) {
  while (!digits.empty() && digits.back() == '0') digits.pop_back();
  return digits;
}

std::string formatMetric(std::int64_t nm, std::int64_t unit, const char* suffix) {
  const bool negative = nm < 0;
  std::int64_t magnitude = nm;
  if (negative) magnitude = -magnitude;
  const std::int64_t whole = magnitude / unit;
  std::int64_t remainder = magnitude % unit;
  int digits = 0;
  for (std::int64_t probe = unit; probe > 1; probe /= 10) ++digits;
  std::string text = (negative ? "-" : "") + std::to_string(whole);
  if (remainder != 0) {
    std::string frac = std::to_string(remainder);
    if (static_cast<int>(frac.size()) < digits) frac.insert(0, static_cast<std::size_t>(digits - frac.size()), '0');
    frac = trimZeros(frac);
    text.push_back('.');
    text += frac;
  }
  text += suffix;
  return text;
}

int gcdInt(int a, int b) {
  while (b != 0) {
    const int rest = a % b;
    a = b;
    b = rest;
  }
  return a;
}

std::string formatInexactInches(std::int64_t magnitude) {
  const std::int64_t whole = magnitude / kNmPerInch;
  const std::int64_t remainder = magnitude % kNmPerInch;
  if (remainder == 0) return std::to_string(whole);
  std::string fallback = std::to_string(whole);
  for (int digits = 1; digits <= 12; ++digits) {
    const std::int64_t scale = pow10(digits);
    std::int64_t ticks = 0;
    if (!mulDivRound(remainder, scale, kNmPerInch, false, ticks)) continue;
    if (ticks >= scale) return std::to_string(whole + 1);
    std::string frac = std::to_string(ticks);
    if (static_cast<int>(frac.size()) < digits) {
      frac.insert(0, static_cast<std::size_t>(digits - static_cast<int>(frac.size())), '0');
    }
    while (!frac.empty() && frac.back() == '0') frac.pop_back();
    std::string numeric = std::to_string(whole);
    if (!frac.empty()) {
      numeric.push_back('.');
      numeric += frac;
    }
    fallback = numeric;
    const auto parsed = parseLength(numeric + " in", LengthUnit::Inch);
    if (parsed.ok && parsed.nm == magnitude) return numeric;
  }
  return fallback;
}

std::string formatInches(std::int64_t nm, int denom, bool withFeet) {
  const bool negative = nm < 0;
  std::int64_t magnitude = negative ? -nm : nm;
  std::string prefix = negative ? "-" : "";
  std::int64_t feet = 0;
  if (withFeet) {
    feet = magnitude / kNmPerFoot;
    magnitude %= kNmPerFoot;
  }

  const bool exact = denom > 0 && (magnitude * static_cast<std::int64_t>(denom)) % kNmPerInch == 0;
  std::string inches;
  if (exact) {
    const std::int64_t ticks = magnitude * denom / kNmPerInch;
    const std::int64_t whole = ticks / denom;
    int numerator = static_cast<int>(ticks % denom);
    int denominator = denom;
    const int divisor = gcdInt(numerator, denominator);
    if (divisor > 0) {
      numerator /= divisor;
      denominator /= divisor;
    }
    if (whole != 0 && numerator != 0) {
      inches = std::to_string(whole) + " " + std::to_string(numerator) + "/" + std::to_string(denominator) + "\"";
    } else if (numerator != 0) {
      inches = std::to_string(numerator) + "/" + std::to_string(denominator) + "\"";
    } else if (whole != 0 || feet == 0) {
      inches = std::to_string(whole) + "\"";
    }
  } else {
    inches = formatInexactInches(magnitude) + " in";
  }

  if (!withFeet || feet == 0) return prefix + inches;
  if (inches.empty()) return prefix + std::to_string(feet) + "'";
  return prefix + std::to_string(feet) + "' " + inches;
}

}  // namespace

ParseResult parseLength(std::string_view text, LengthUnit defaultUnit) {
  if (text.size() > 64) return fail("units.invalid");
  const std::string lowered = asciiLower(text);
  Scan scan{lowered, 0};
  scan.skip();
  if (scan.eof()) return fail("units.empty");

  Number number;
  if (!parseNumber(scan, number)) return fail("units.invalid");
  scan.skip();
  if (scan.peek() == '/') return finishFraction(scan, number, defaultUnit);

  UnitKind kind = UnitKind::None;
  const bool gotUnit = consumeUnit(scan, kind);
  if (gotUnit && kind == UnitKind::Foot) return finishFeet(scan, number, defaultUnit);
  if (!gotUnit && !number.hasFrac && looksLikeMixed(scan)) return finishMixed(scan, number, defaultUnit);
  if (!gotUnit) consumeUnit(scan, kind);
  return finishSimple(scan, number, kind, defaultUnit);
}

std::string formatLength(std::int64_t nm, LengthUnit unit, int fractionDenom) {
  if (!validFractionDenom(fractionDenom)) fractionDenom = 16;
  switch (unit) {
    case LengthUnit::Millimetre:
      return formatMetric(nm, kNmPerMm, " mm");
    case LengthUnit::Centimetre:
      return formatMetric(nm, kNmPerCm, " cm");
    case LengthUnit::Metre:
      return formatMetric(nm, kNmPerMetre, " m");
    case LengthUnit::Inch:
      return formatInches(nm, fractionDenom, false);
    case LengthUnit::Foot:
      return formatInches(nm, fractionDenom, true);
  }
  return formatMetric(nm, kNmPerMm, " mm");
}

bool lengthUnitFromKey(std::string_view key, LengthUnit& out) {
  if (key == "mm") {
    out = LengthUnit::Millimetre;
    return true;
  }
  if (key == "cm") {
    out = LengthUnit::Centimetre;
    return true;
  }
  if (key == "m") {
    out = LengthUnit::Metre;
    return true;
  }
  if (key == "in") {
    out = LengthUnit::Inch;
    return true;
  }
  if (key == "ft") {
    out = LengthUnit::Foot;
    return true;
  }
  return false;
}

const char* lengthUnitKey(LengthUnit unit) {
  switch (unit) {
    case LengthUnit::Millimetre:
      return "mm";
    case LengthUnit::Centimetre:
      return "cm";
    case LengthUnit::Metre:
      return "m";
    case LengthUnit::Inch:
      return "in";
    case LengthUnit::Foot:
      return "ft";
  }
  return "mm";
}

bool validFractionDenom(int denom) {
  return denom == 2 || denom == 4 || denom == 8 || denom == 16 || denom == 32 || denom == 64;
}

double toMillimetres(std::int64_t nm) { return static_cast<double>(nm) / static_cast<double>(kNmPerMm); }

AngleParse parseAngle(std::string_view text) {
  if (text.size() > 64) {
    AngleParse result;
    result.error = "angle.invalid";
    return result;
  }
  const std::string lowered = asciiLower(text);
  Scan scan{lowered, 0};
  Number number;
  if (!parseNumber(scan, number)) {
    AngleParse result;
    result.error = "angle.invalid";
    return result;
  }
  scan.skip();
  if (scan.consume("°") || scan.consume("deg")) {
    scan.skip();
  }
  if (!scan.eof()) {
    AngleParse result;
    result.error = "angle.trailing";
    return result;
  }
  const double scale = number.hasFrac ? static_cast<double>(pow10(number.fracDigits)) : 1.0;
  double value = static_cast<double>(number.whole) +
                 (number.hasFrac ? static_cast<double>(number.frac) / scale : 0.0);
  if (number.negative) value = -value;
  if (!std::isfinite(value) || std::abs(value) > kMaxAbsDegrees) {
    AngleParse result;
    result.error = "angle.invalid";
    return result;
  }
  AngleParse result;
  result.ok = true;
  result.degrees = value;
  return result;
}

std::string formatAngle(double degrees) {
  if (!std::isfinite(degrees)) return "0\u00B0";
  const double rounded = std::round(degrees * 1000.0) / 1000.0;
  std::string text = std::to_string(rounded);
  const auto dot = text.find('.');
  if (dot != std::string::npos) {
    while (!text.empty() && text.back() == '0') text.pop_back();
    if (!text.empty() && text.back() == '.') text.pop_back();
  }
  text += "\u00B0";
  return text;
}

}  // namespace timberlab::units
