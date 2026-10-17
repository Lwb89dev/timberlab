#include "timberlab/constraints/dependency_graph.hpp"

#include <algorithm>
#include <queue>
#include <unordered_map>
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

std::vector<std::string> DependencyGraph::topologicalOrder() const {
  std::unordered_map<std::string, int> incoming;
  incoming.reserve(outgoing_.size());
  for (const auto& entry : outgoing_) incoming.emplace(entry.first, 0);
  for (const auto& entry : outgoing_) {
    for (const std::string& next : entry.second) ++incoming[next];
  }

  std::vector<std::string> ready;
  ready.reserve(incoming.size());
  for (const auto& entry : incoming) {
    if (entry.second == 0) ready.push_back(entry.first);
  }
  std::sort(ready.begin(), ready.end());

  std::vector<std::string> order;
  order.reserve(incoming.size());
  std::size_t cursor = 0;
  while (cursor < ready.size()) {
    const std::string current = ready[cursor++];
    order.push_back(current);
    const auto found = outgoing_.find(current);
    if (found == outgoing_.end()) continue;
    std::vector<std::string> unlocked;
    for (const std::string& next : found->second) {
      int& count = incoming[next];
      --count;
      if (count == 0) unlocked.push_back(next);
    }
    std::sort(unlocked.begin(), unlocked.end());
    ready.insert(ready.end(), unlocked.begin(), unlocked.end());
  }
  if (order.size() != incoming.size()) return {};
  return order;
}

}  // namespace timberlab::constraints
