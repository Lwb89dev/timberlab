#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace timberlab::constraints {

// Directed edge "from -> to" means changing `from` affects `to`.
// The graph is the seed of later parametric propagation. It does not
// recompute furniture geometry yet.
class DependencyGraph {
 public:
  void clear();
  void addNode(const std::string& id);
  void addEdge(const std::string& from, const std::string& to);

  bool hasCycle() const;
  std::vector<std::string> affectedBy(const std::string& changed) const;

 private:
  std::unordered_map<std::string, std::vector<std::string>> outgoing_;
};

}  // namespace timberlab::constraints
