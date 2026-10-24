#include "timberlab/materials/catalog.hpp"

#include <nlohmann/json.hpp>

namespace timberlab::materials {
namespace {

bool readOptionalNumber(const nlohmann::json& item, const char* key, std::optional<double>& out, std::string& error) {
  if (!item.contains(key) || item[key].is_null()) return true;
  if (!item[key].is_number()) {
    error = "material.number";
    return false;
  }
  out = item[key].get<double>();
  return true;
}

}  // namespace

bool Catalog::load(const std::string& json, std::string& error) {
  nlohmann::json root;
  try {
    root = nlohmann::json::parse(json);
  } catch (const nlohmann::json::exception&) {
    error = "material.json";
    return false;
  }
  if (!root.is_object() || root.value("schema", "") != "timberlab.materials") {
    error = "material.format";
    return false;
  }
  if (root.value("schemaVersion", 0) != 2 || !root.contains("entries") || !root["entries"].is_array()) {
    error = "material.format";
    return false;
  }

  std::vector<CatalogEntry> entries;
  for (const auto& item : root["entries"]) {
    CatalogEntry entry;
    entry.id = item.value("id", "");
    entry.category = item.value("category", "");
    entry.scientificName = item.value("scientificName", "");
    entry.visualHex = item.value("visualHex", "");
    entry.faceTexture = item.value("faceTexture", "");
    entry.endTexture = item.value("endTexture", "");
    entry.identityAmbiguous = item.value("identityAmbiguous", false);
    entry.grain = item.value("grain", "");
    entry.source = item.value("source", "");
    entry.notes = item.value("notes", "");
    if (entry.id.empty() || entry.category.empty() || entry.visualHex.size() != 7 || entry.visualHex[0] != '#' ||
        entry.faceTexture.empty() || entry.endTexture.empty()) {
      error = "material.json";
      return false;
    }
    if (!item.contains("commonNames") || !item["commonNames"].is_object() || !item["commonNames"].contains("en")) {
      error = "material.json";
      return false;
    }
    for (auto it = item["commonNames"].begin(); it != item["commonNames"].end(); ++it) {
      if (!it.value().is_string()) {
        error = "material.json";
        return false;
      }
      entry.commonNames.emplace_back(it.key(), it.value().get<std::string>());
    }
    if (!readOptionalNumber(item, "densityKgM3", entry.densityKgM3, error)) return false;
    if (!readOptionalNumber(item, "jankaN", entry.jankaN, error)) return false;
    entries.push_back(std::move(entry));
  }
  entries_ = std::move(entries);
  return true;
}

const CatalogEntry* Catalog::find(std::string_view id) const {
  for (const CatalogEntry& entry : entries_) {
    if (entry.id == id) return &entry;
  }
  return nullptr;
}

std::string Catalog::displayName(std::string_view id, std::string_view language) const {
  const CatalogEntry* entry = find(id);
  if (entry == nullptr) return std::string(id);
  std::string english;
  for (const auto& name : entry->commonNames) {
    if (name.first == language) return name.second;
    if (name.first == "en") english = name.second;
  }
  if (!english.empty()) return english;
  return std::string(id);
}

bool loadPresets(const std::string& json, std::vector<ThicknessPreset>& thicknesses,
                 std::vector<LumberPreset>& lumber, std::string& error) {
  nlohmann::json root;
  try {
    root = nlohmann::json::parse(json);
  } catch (const nlohmann::json::exception&) {
    error = "preset.json";
    return false;
  }
  if (root.value("schema", "") != "timberlab.presets" || root.value("schemaVersion", 0) != 1) {
    error = "preset.format";
    return false;
  }
  thicknesses.clear();
  lumber.clear();
  for (const auto& item : root["thicknesses"]) {
    ThicknessPreset preset;
    preset.id = item.value("id", "");
    preset.group = item.value("group", "");
    preset.label = item.value("label", "");
    if (preset.id.empty() || !item.contains("nm") || !item["nm"].is_number_integer()) {
      error = "preset.json";
      return false;
    }
    preset.thicknessNm = item["nm"].get<std::int64_t>();
    thicknesses.push_back(std::move(preset));
  }
  for (const auto& item : root["lumber"]) {
    LumberPreset preset;
    preset.id = item.value("id", "");
    preset.nominal = item.value("nominal", "");
    preset.note = item.value("note", "");
    if (preset.id.empty() || !item["roughThicknessNm"].is_number_integer() ||
        !item["surfacedThicknessNm"].is_number_integer() || !item["surfacedWidthNm"].is_number_integer()) {
      error = "preset.json";
      return false;
    }
    preset.roughThicknessNm = item["roughThicknessNm"].get<std::int64_t>();
    preset.surfacedThicknessNm = item["surfacedThicknessNm"].get<std::int64_t>();
    preset.surfacedWidthNm = item["surfacedWidthNm"].get<std::int64_t>();
    lumber.push_back(std::move(preset));
  }
  return true;
}

}  // namespace timberlab::materials
