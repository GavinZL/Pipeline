#include "pipeline/core/PipelineError.h"
#include <sstream>

namespace pipeline {

PipelineError::PipelineError(int code, std::string message,
                              ErrorCategory category,
                              std::optional<std::string> suggestion)
    : m_code(code)
    , m_message(std::move(message))
    , m_category(category)
    , m_suggestion(std::move(suggestion)) {}

std::string PipelineError::toString() const {
    std::ostringstream oss;
    oss << "[" << categoryToString(m_category) << "] "
        << "(Code: " << m_code << ") "
        << m_message;
    
    if (m_suggestion) {
        oss << " | Suggestion: " << *m_suggestion;
    }
    
    return oss.str();
}

const char* PipelineError::categoryToString(ErrorCategory category) {
    switch (category) {
        case ErrorCategory::None:           return "Success";
        case ErrorCategory::InvalidArgument:return "InvalidArgument";
        case ErrorCategory::Initialization: return "Initialization";
        case ErrorCategory::Platform:       return "Platform";
        case ErrorCategory::Resource:       return "Resource";
        case ErrorCategory::Runtime:        return "Runtime";
        case ErrorCategory::NotSupported:   return "NotSupported";
        case ErrorCategory::Internal:       return "Internal";
        default:                            return "Unknown";
    }
}

PipelineError PipelineError::invalidArgument(const std::string& details) {
    return PipelineError(
        1001,
        "Invalid argument: " + details,
        ErrorCategory::InvalidArgument,
        "Check the parameter values and try again"
    );
}

PipelineError PipelineError::initializationFailed(const std::string& details) {
    return PipelineError(
        2001,
        "Initialization failed: " + details,
        ErrorCategory::Initialization,
        "Verify configuration and platform support"
    );
}

PipelineError PipelineError::platformError(const std::string& details) {
    return PipelineError(
        3001,
        "Platform error: " + details,
        ErrorCategory::Platform,
        "Check platform-specific requirements and permissions"
    );
}

PipelineError PipelineError::resourceError(const std::string& details) {
    return PipelineError(
        4001,
        "Resource error: " + details,
        ErrorCategory::Resource,
        "Free up system resources and try again"
    );
}

PipelineError PipelineError::runtimeError(const std::string& details) {
    return PipelineError(
        5001,
        "Runtime error: " + details,
        ErrorCategory::Runtime,
        "Retry the operation or restart the pipeline"
    );
}

PipelineError PipelineError::notSupported(const std::string& details) {
    return PipelineError(
        6001,
        "Operation not supported: " + details,
        ErrorCategory::NotSupported,
        "Use alternative API or upgrade the framework"
    );
}

PipelineError PipelineError::internalError(const std::string& details) {
    return PipelineError(
        9001,
        "Internal error: " + details,
        ErrorCategory::Internal,
        "Report this issue to the development team"
    );
}

} // namespace pipeline
