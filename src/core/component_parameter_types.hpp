#pragma once
// The component parameter record pointer and override map, apart from the records
// (core/component_parameter_records.hpp) so that core/component_records.hpp needs only these
// (build fan-out, ADR 0170).
#include "core/parameter_expression.hpp"
#include <map>
#include <memory>
#include <string>
namespace sketchy {
struct ComponentParameters;
using ComponentParametersPtr = std::shared_ptr<const ComponentParameters>;
// Override values keyed by parameter name: SI quantities (finalized and quantized
// like evaluated values), booleans or choice values.
using ComponentOverrides = std::map<std::string, ParameterValue>;
} // namespace sketchy
