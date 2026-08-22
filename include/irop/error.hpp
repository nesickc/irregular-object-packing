#pragma once

#include <stdexcept>
#include <string>

namespace irop {

enum class ErrorCategory {
    invalid_configuration,
    input_io,
    resource_limit,
    invalid_mesh,
    output_io,
    dependency_failure,
    internal,
};

[[nodiscard]] const char* to_string(ErrorCategory category) noexcept;

class Error final : public std::runtime_error {
public:
    Error(ErrorCategory category, std::string message);

    [[nodiscard]] ErrorCategory category() const noexcept;

private:
    ErrorCategory category_;
};

}  // namespace irop
