#pragma once

#include "timberlab/core/component.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace timberlab::constraints {

// A project driver. valueNm is a length. Zero is allowed. Negative is not.
struct Parameter {
  std::string id;
  std::string name;
  std::int64_t valueNm = 0;
};

// One addend. Exactly one of parameterId or (partId + field) is set.
// coefficient is a non-zero integer. The contribution is coefficient * source.
struct Term {
  std::string parameterId;
  std::string partId;
  std::string field;
  std::int64_t coefficient = 1;
};

// partId.field = constantNm + sum(terms).
// field is width, height, thickness, radius, x, y, or z.
struct Binding {
  std::string partId;
  std::string field;
  std::int64_t constantNm = 0;
  std::vector<Term> terms;
};

bool isBindingField(std::string_view field);

// Writes every bound field. On failure `components` is left unchanged.
// Empty bindings succeed without reading the parts.
bool propagateBindings(const std::vector<Parameter>& parameters, const std::vector<Binding>& bindings,
                       std::vector<Component>& components, std::string& error);

}  // namespace timberlab::constraints
