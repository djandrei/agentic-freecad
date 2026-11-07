// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include <Mod/Agentic/AgenticGlobal.h>

namespace Agentic
{

/// Validates a value against the subset of JSON Schema the tool schemas use.
///
/// Supported keywords: `type` (a name or a list of names), `properties`,
/// `required`, `additionalProperties` (boolean), `enum`, `const`, `items`,
/// `minItems`, `maxItems`, `minimum`, `maximum`, `minLength`, `maxLength`.
/// Returns nothing when the value is valid, otherwise a message that names
/// the offending path, e.g. "arguments.width: expected integer, got string".
AgenticExport std::optional<std::string> validateJson(
    const nlohmann::json& schema,
    const nlohmann::json& value,
    const std::string& path = "arguments"
);

}  // namespace Agentic
