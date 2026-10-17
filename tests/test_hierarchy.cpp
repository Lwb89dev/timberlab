#include "timberlab/core/document.hpp"

#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>

using timberlab::Document;

namespace {

const nlohmann::json* componentNamed(const nlohmann::json& root, const char* name) {
  for (const nlohmann::json& item : root["components"]) {
    if (item.value("name", "") == name) return &item;
  }
  return nullptr;
}

}  // namespace

TEST_CASE("a parent link rejects a cycle, a lock, and a missing part") {
  Document document;
  std::string error;
  const std::string child = document.addBoard();
  const std::string parent = document.addBoard();
  REQUIRE(document.setParent(child, parent, error));
  REQUIRE(document.find(child)->parentId == parent);
  REQUIRE(document.canUndo());

  const std::string linked = document.toJson();
  REQUIRE(document.setParent(child, parent, error));
  REQUIRE(document.toJson() == linked);

  REQUIRE(document.undo(error));
  REQUIRE(document.find(child)->parentId.empty());
  REQUIRE(document.setParent(child, parent, error));

  REQUIRE_FALSE(document.setParent(child, child, error));
  REQUIRE(error == "component.cycle");
  REQUIRE(document.find(child)->parentId == parent);

  REQUIRE_FALSE(document.setParent(parent, child, error));
  REQUIRE(error == "component.cycle");

  REQUIRE_FALSE(document.setParent(child, "missing", error));
  REQUIRE(error == "component.missing");

  REQUIRE(document.setLocked(child, true, error));
  REQUIRE_FALSE(document.setParent(child, "", error));
  REQUIRE(error == "component.locked");
  REQUIRE(document.find(child)->parentId == parent);

  REQUIRE(document.setLocked(child, false, error));
  REQUIRE(document.setParent(child, "", error));
  REQUIRE(document.find(child)->parentId.empty());
}

TEST_CASE("removing a parent clears the link on a locked child") {
  Document document;
  std::string error;
  const std::string child = document.addBoard();
  const std::string parent = document.addBoard();
  REQUIRE(document.setParent(child, parent, error));
  REQUIRE(document.setLocked(child, true, error));
  REQUIRE(document.remove(parent, error));
  REQUIRE(document.find(child)->parentId.empty());
  REQUIRE(document.find(child)->locked);
}

TEST_CASE("load rejects a missing parent and a cycle") {
  Document document;
  document.addBoard();
  document.addBoard();
  nlohmann::json root = nlohmann::json::parse(document.toJson());
  root["components"][0]["parentId"] = "missing";
  Document loaded;
  std::string error;
  REQUIRE_FALSE(Document::fromJson(root.dump(), loaded, error));
  REQUIRE(error == "component.missing");

  root["components"][0]["parentId"] = root["components"][1]["id"];
  root["components"][1]["parentId"] = root["components"][0]["id"];
  REQUIRE_FALSE(Document::fromJson(root.dump(), loaded, error));
  REQUIRE(error == "component.cycle");
}

TEST_CASE("a role is a token and an empty role is omitted") {
  Document document;
  std::string error;
  const std::string id = document.addBoard();
  REQUIRE(document.setRole(id, "leg", error));
  REQUIRE(document.find(id)->role == "leg");
  Document loaded;
  REQUIRE(Document::fromJson(document.toJson(), loaded, error));
  REQUIRE(loaded.find(id)->role == "leg");

  const std::string tagged = document.toJson();
  REQUIRE(document.setRole(id, "leg", error));
  REQUIRE(document.toJson() == tagged);

  REQUIRE_FALSE(document.setRole(id, "Leg", error));
  REQUIRE(error == "component.role");
  REQUIRE(document.find(id)->role == "leg");

  REQUIRE(document.setRole(id, "", error));
  REQUIRE(document.find(id)->role.empty());
  const nlohmann::json cleared = nlohmann::json::parse(document.toJson());
  REQUIRE(componentNamed(cleared, "Board 1") != nullptr);
  REQUIRE_FALSE(componentNamed(cleared, "Board 1")->contains("role"));
}
