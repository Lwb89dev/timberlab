#include "timberlab/materials/catalog.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

TEST_CASE("the shipped catalog names at least forty species and invents no numbers") {
  const std::filesystem::path file =
      std::filesystem::path(TIMBERLAB_SOURCE_DIR) / "resources/data/materials.json";
  std::ifstream input(file);
  REQUIRE(input);
  std::stringstream buffer;
  buffer << input.rdbuf();
  timberlab::materials::Catalog catalog;
  std::string error;
  REQUIRE(catalog.load(buffer.str(), error));

  int species = 0;
  bool sawOak = false;
  for (const auto& entry : catalog.entries()) {
    if (entry.category == "hardwood" || entry.category == "softwood") ++species;
    if (!entry.identityAmbiguous && entry.category != "engineered") {
      REQUIRE(entry.scientificName.find(' ') != std::string::npos);
    }
    REQUIRE_FALSE(entry.densityKgM3.has_value());
    REQUIRE_FALSE(entry.jankaN.has_value());
    REQUIRE(entry.visualHex.size() == 7);
    if (entry.id == "european-oak") {
      sawOak = true;
      REQUIRE(entry.grain == "ring-porous");
      REQUIRE(catalog.displayName(entry.id, "it") == "Rovere europeo");
    }
    if (entry.id == "purpleheart") REQUIRE(entry.identityAmbiguous);
  }
  REQUIRE(species >= 40);
  REQUIRE(sawOak);
  REQUIRE(catalog.find("black-walnut") != nullptr);
  REQUIRE(catalog.find("european-beech") != nullptr);
  REQUIRE(catalog.find("birch-plywood") != nullptr);
}

TEST_CASE("presets keep nominal lumber apart from surfaced size") {
  const std::filesystem::path file = std::filesystem::path(TIMBERLAB_SOURCE_DIR) / "resources/data/presets.json";
  std::ifstream input(file);
  REQUIRE(input);
  std::stringstream buffer;
  buffer << input.rdbuf();
  std::vector<timberlab::materials::ThicknessPreset> thicknesses;
  std::vector<timberlab::materials::LumberPreset> lumber;
  std::string error;
  REQUIRE(timberlab::materials::loadPresets(buffer.str(), thicknesses, lumber, error));
  REQUIRE_FALSE(thicknesses.empty());

  const auto twoByFour = std::find_if(lumber.begin(), lumber.end(), [](const auto& item) {
    return item.id == "dim-2x4";
  });
  REQUIRE(twoByFour != lumber.end());
  REQUIRE(twoByFour->roughThicknessNm == 50'800'000);
  REQUIRE(twoByFour->surfacedThicknessNm == 38'100'000);
  REQUIRE(twoByFour->surfacedWidthNm == 88'900'000);
  REQUIRE(twoByFour->roughThicknessNm != twoByFour->surfacedThicknessNm);
}
