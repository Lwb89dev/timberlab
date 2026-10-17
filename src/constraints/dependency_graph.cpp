#include "timberlab/constraints/dependency_graph.hpp"

#include <queue>
#include <unordered_set>

namespace timberlab::constraints {

void DependencyGraph::clear() { outgoing_.clear(); }

void DependencyGraph::addNode(const std::string& id) { outgoing_[id]; }

void DependencyGraph::addEdge(const std::string& from, const std::string& to) {
  outgoing_[from].push_back(to);
  outgoing_[to];
}

bool DependencyGraph::hasCycle() const {
  enum class Color { White, Grey, Black };
  std::unordered_map<std::string, Color> color;
  for (const auto& entry : outgoing_) color[entry.first] = Color::White;

  const auto visit = [&](auto&& self, const std::string& id) -> bool {
    color[id] = Color::Grey;
    const auto found = outgoing_.find(id);
    if (found != outgoing_.end()) {
      for (const std::string& next : found->second) {
        const Color state = color[next];
        if (state == Color::Grey) return true;
        if (state == Color::White && self(self, next)) return true;
      }
    }
    color[id] = Color::Black;
    return false;
  };

  for (const auto& entry : outgoing_) {
    if (color[entry.first] == Color::White && visit(visit, entry.first)) return true;
  }
  return false;
}

std::vector<std::string> DependencyGraph::affectedBy(const std::string& changed) const {
  std::vector<std::string> result;
  std::unordered_set<std::string> seen;
  std::queue<std::string> waiting;
  waiting.push(changed);
  seen.insert(changed);
  while (!waiting.empty()) {
    const std::string current = waiting.front();
    waiting.pop();
    const auto found = outgoing_.find(current);
    if (found == outgoing_.end()) continue;
    for (const std::string& next : found->second) {
      if (!seen.insert(next).second) continue;
      result.push_back(next);
      waiting.push(next);
    }
  }
  return result;
}

}  // namespace timberlab::constraints
