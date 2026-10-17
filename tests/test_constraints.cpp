#include "timberlab/constraints/dependency_graph.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

TEST_CASE("dependency edges report descendants and cycles") {
  timberlab::constraints::DependencyGraph graph;
  graph.addEdge("width", "apron");
  graph.addEdge("apron", "tenon");
  graph.addEdge("width", "leg");
  REQUIRE_FALSE(graph.hasCycle());
  REQUIRE(graph.affectedBy("width").size() == 3);
  const std::vector<std::string> order = graph.topologicalOrder();
  REQUIRE(order.size() == 4);
  const auto index = [&](const char* id) {
    return static_cast<int>(std::find(order.begin(), order.end(), id) - order.begin());
  };
  REQUIRE(index("width") < index("apron"));
  REQUIRE(index("apron") < index("tenon"));
  REQUIRE(index("width") < index("leg"));

  timberlab::constraints::DependencyGraph repeated;
  repeated.addEdge("width", "apron");
  repeated.addEdge("width", "apron");
  REQUIRE_FALSE(repeated.hasCycle());
  REQUIRE(repeated.topologicalOrder().size() == 2);

  graph.addEdge("tenon", "width");
  REQUIRE(graph.hasCycle());
  REQUIRE(graph.topologicalOrder().empty());
}
