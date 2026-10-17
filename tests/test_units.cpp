#include "timberlab/units/units.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using timberlab::units::LengthUnit;

TEST_CASE("imperial presets are exact nanometres") {
  const std::pair<const char*, std::int64_t> presets[] = {
      {"1/8\"", 3'175'000},  {"1/4\"", 6'350'000},  {"3/8\"", 9'525'000},   {"1/2\"", 12'700'000},
      {"5/8\"", 15'875'000}, {"3/4\"", 19'050'000}, {"1\"", 25'400'000},    {"1 1/4\"", 31'750'000},
      {"1 1/2\"", 38'100'000}, {"2\"", 50'800'000}, {"3\"", 76'200'000},    {"4\"", 101'600'000},
  };
  for (const auto& preset : presets) {
    const auto parsed = timberlab::units::parseLength(preset.first, LengthUnit::Millimetre);
    REQUIRE(parsed.ok);
    REQUIRE(parsed.nm == preset.second);
    const std::string shown = timberlab::units::formatLength(parsed.nm, LengthUnit::Inch, 64);
    const auto again = timberlab::units::parseLength(shown, LengthUnit::Millimetre);
    REQUIRE(again.ok);
    REQUIRE(again.nm == preset.second);
  }
}

TEST_CASE("sixty four sixty-fourths make one inch") {
  std::int64_t sum = 0;
  for (int i = 0; i < 64; ++i) {
    const auto step = timberlab::units::parseLength("1/64\"", LengthUnit::Inch);
    REQUIRE(step.ok);
    sum += step.nm;
  }
  REQUIRE(sum == timberlab::units::kNmPerInch);
}

TEST_CASE("feet inches and comma decimals") {
  const auto mixed = timberlab::units::parseLength("3' 6 1/4\"", LengthUnit::Millimetre);
  const std::int64_t expected =
      3 * timberlab::units::kNmPerFoot + 6 * timberlab::units::kNmPerInch + timberlab::units::kNmPerInch / 4;
  REQUIRE(mixed.ok);
  REQUIRE(mixed.nm == expected);

  const auto comma = timberlab::units::parseLength("1,5 cm", LengthUnit::Millimetre);
  REQUIRE(comma.ok);
  REQUIRE(comma.nm == 15 * timberlab::units::kNmPerMm);

  REQUIRE(timberlab::units::formatLength(19'050'000, LengthUnit::Millimetre, 16) == "19.05 mm");
}

TEST_CASE("display rounding is not applied to an inexact fraction") {
  const auto parsed = timberlab::units::parseLength("0.2 mm", LengthUnit::Millimetre);
  REQUIRE(parsed.ok);
  REQUIRE(parsed.nm == 200'000);
  const std::string inches = timberlab::units::formatLength(parsed.nm, LengthUnit::Inch, 16);
  REQUIRE(inches.find('/') == std::string::npos);
  const auto back = timberlab::units::parseLength(inches, LengthUnit::Inch);
  REQUIRE(back.ok);
  REQUIRE(back.nm == parsed.nm);
}

TEST_CASE("empty and garbage lengths fail") {
  REQUIRE_FALSE(timberlab::units::parseLength("", LengthUnit::Millimetre).ok);
  REQUIRE_FALSE(timberlab::units::parseLength("oak", LengthUnit::Millimetre).ok);
  const auto huge = timberlab::units::parseLength(std::string(65, '1'), LengthUnit::Millimetre);
  REQUIRE_FALSE(huge.ok);
  REQUIRE(huge.error == "units.invalid");
  const auto wild = timberlab::units::parseAngle(std::string(65, '9'));
  REQUIRE_FALSE(wild.ok);
  REQUIRE(wild.error == "angle.invalid");
  REQUIRE(timberlab::units::validFractionDenom(16));
  REQUIRE_FALSE(timberlab::units::validFractionDenom(3));
}
