// A small JSON syntax checker for tests (validates the reports emitted by the toolchain).
#pragma once

#include <string_view>

namespace test
{
    // True if 'text' is exactly one valid JSON value (RFC 8259 syntax).
    bool is_valid_json(std::string_view text);
}
