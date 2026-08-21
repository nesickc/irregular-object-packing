#include "irop/error.hpp"

#include <utility>

namespace irop {

const char* to_string(const ErrorCategory category) noexcept
{
    switch (category) {
    case ErrorCategory::input_io:
        return "input_io";
    case ErrorCategory::resource_limit:
        return "resource_limit";
    case ErrorCategory::invalid_mesh:
        return "invalid_mesh";
    case ErrorCategory::output_io:
        return "output_io";
    case ErrorCategory::dependency_failure:
        return "dependency_failure";
    case ErrorCategory::internal:
        return "internal";
    }

    return "internal";
}

Error::Error(const ErrorCategory category, std::string message) :
    std::runtime_error(std::move(message)),
    category_(category)
{
}

ErrorCategory Error::category() const noexcept { return category_; }

}  // namespace irop
