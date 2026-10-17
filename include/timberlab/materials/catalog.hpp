#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace timberlab::materials {

struct CatalogEntry {
  std::string id;
  std::string category;
  std::string scientificName;
  std::string visualHex;
  bool identityAmbiguous = false;
  std::vector<std::pair<std::string, std::string>> commonNames;
  // Empty when the record does not state a grain class. Do not invent one.
  std::string grain;
  std::optional<double> densityKgM3;
  // Newtons, Janka hardness. Absent until a cited source is attached.
  std::optional<double> jankaN;
  std::string source;
  std::string notes;
};

struct ThicknessPreset {
  std::string id;
  std::string group;
  std::string label;
  std::int64_t thicknessNm = 0;
};

struct LumberPreset {
  std::string id;
  std::string nominal;
  std::int64_t roughThicknessNm = 0;
  std::int64_t surfacedThicknessNm = 0;
  std::int64_t surfacedWidthNm = 0;
  std::string note;
};

class Catalog {
 public:
  bool load(const std::string& json, std::string& error);
  const std::vector<CatalogEntry>& entries() const { return entries_; }
  const CatalogEntry* find(std::string_view id) const;
  std::string displayName(std::string_view id, std::string_view language) const;

 private:
  std::vector<CatalogEntry> entries_;
};

bool loadPresets(const std::string& json, std::vector<ThicknessPreset>& thicknesses,
                 std::vector<LumberPreset>& lumber, std::string& error);

}  // namespace timberlab::materials
