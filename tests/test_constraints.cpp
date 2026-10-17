#include "timberlab/constraints/dependency_graph.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("dependency edges report descendants and cycles") {
  timberlab::constraints::DependencyGraph graph;
  graph.addEdge("width", "apron");
  graph.addEdge("apron", "tenon");
  graph.addEdge("width", "leg");
  REQUIRE_FALSE(graph.hasCycle());
  REQUIRE(graph.affectedBy("width").size() == 3);
  graph.addEdge("tenon", "width");
  REQUIRE(graph.hasCycle());
}
