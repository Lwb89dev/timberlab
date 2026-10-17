#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace timberlab::constraints {

// Directed edge "from -> to" means changing `from` affects `to`.
// topologicalOrder is the order in which bindings are evaluated.
// An empty result means the graph is empty or it contains a cycle.
class DependencyGraph {
 public:
  void clear();
  void addNode(const std::string& id);
  void addEdge(const std::string& from, const std::string& to);

  bool hasCycle() const;
  std::vector<std::string> affectedBy(const std::string& changed) const;
  std::vector<std::string> topologicalOrder() const;

 private:
  std::unordered_map<std::string, std::vector<std::string>> outgoing_;
};

}  // namespace timberlab::constraints
