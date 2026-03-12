#pragma once

#include <string>
#include <optional>
#include <ostream>

namespace pipeline {

/**
 * @brief 错误类别枚举
 */
enum class ErrorCategory : uint8_t {
    None,           ///< 无错误
    InvalidArgument,///< 无效参数
    Initialization, ///< 初始化失败
    Platform,       ///< 平台错误
    Resource,       ///< 资源错误
    Runtime,        ///< 运行时错误
    NotSupported,   ///< 不支持的操作
    Internal        ///< 内部错误
};

/**
 * @brief Pipeline 错误信息类
 * 
 * 提供详细的错误信息，包括错误码、消息、类别和恢复建议
 * 
 * 使用示例：
 * @code
 * Result<void> result = pipeline->start();
 * if (!result) {
 *     std::cerr << result.error().message() << std::endl;
 *     std::cerr << "Suggestion: " << result.error().suggestion() << std::endl;
 * }
 * @endcode
 */
class PipelineError {
public:
    /**
     * @brief 默认构造函数 - 表示无错误
     */
    PipelineError() = default;
    
    /**
     * @brief 构造函数
     * @param code 错误码
     * @param message 错误消息
     * @param category 错误类别
     * @param suggestion 恢复建议（可选）
     */
    PipelineError(int code, std::string message, 
                  ErrorCategory category = ErrorCategory::Internal,
                  std::optional<std::string> suggestion = std::nullopt);
    
    /**
     * @brief 检查是否有错误
     */
    bool hasError() const { return m_code != 0; }
    
    /**
     * @brief 获取错误码
     */
    int code() const { return m_code; }
    
    /**
     * @brief 获取错误消息
     */
    const std::string& message() const { return m_message; }
    
    /**
     * @brief 获取错误类别
     */
    ErrorCategory category() const { return m_category; }
    
    /**
     * @brief 获取恢复建议
     */
    std::string suggestion() const {
        return m_suggestion.value_or("No suggestion available");
    }
    
    /**
     * @brief 转换为字符串
     */
    std::string toString() const;
    
    /**
     * @brief 获取错误类别的字符串描述
     */
    static const char* categoryToString(ErrorCategory category);
    
    // 预定义常见错误
    static PipelineError success() { return PipelineError(); }
    static PipelineError invalidArgument(const std::string& details);
    static PipelineError initializationFailed(const std::string& details);
    static PipelineError platformError(const std::string& details);
    static PipelineError resourceError(const std::string& details);
    static PipelineError runtimeError(const std::string& details);
    static PipelineError notSupported(const std::string& details);
    static PipelineError internalError(const std::string& details);
    
private:
    int m_code = 0;
    std::string m_message;
    ErrorCategory m_category = ErrorCategory::None;
    std::optional<std::string> m_suggestion;
};

/**
 * @brief 结果模板类（C++17 实现，不使用 C++23 std::expected）
 * 
 * @tparam T 成功时返回的类型
 * 
 * 使用示例：
 * @code
 * Result<int> divide(int a, int b) {
 *     if (b == 0) {
 *         return Result<int>::error(PipelineError::invalidArgument("Division by zero"));
 *     }
 *     return Result<int>::success(a / b);
 * }
 * 
 * auto result = divide(10, 2);
 * if (result) {
 *     std::cout << "Result: " << result.value() << std::endl;
 * } else {
 *     std::cerr << "Error: " << result.error().message() << std::endl;
 * }
 * @endcode
 */
template<typename T>
class Result {
public:
    using ValueType = T;
    
    // 构造函数
    Result() : m_hasValue(false), m_error(PipelineError::success()) {}
    
    Result(T value) : m_hasValue(true), m_value(std::move(value)), 
                      m_error(PipelineError::success()) {}
    
    Result(PipelineError error) : m_hasValue(false), 
                                   m_error(std::move(error)) {}
    
    // 移动构造函数
    Result(Result&& other) noexcept 
        : m_hasValue(other.m_hasValue),
          m_value(std::move(other.m_value)),
          m_error(std::move(other.m_error)) {
        other.m_hasValue = false;
    }
    
    Result& operator=(Result&& other) noexcept {
        if (this != &other) {
            m_hasValue = other.m_hasValue;
            m_value = std::move(other.m_value);
            m_error = std::move(other.m_error);
            other.m_hasValue = false;
        }
        return *this;
    }
    
    // 删除拷贝
    Result(const Result&) = delete;
    Result& operator=(const Result&) = delete;
    
    // 布尔转换 - 检查是否成功
    explicit operator bool() const { return m_hasValue; }
    
    // 访问值
    bool hasValue() const { return m_hasValue; }
    
    T& value() & {
        if (!m_hasValue) {
            throw std::runtime_error("Result does not contain a value");
        }
        return m_value;
    }
    
    const T& value() const & {
        if (!m_hasValue) {
            throw std::runtime_error("Result does not contain a value");
        }
        return m_value;
    }
    
    T&& value() && {
        if (!m_hasValue) {
            throw std::runtime_error("Result does not contain a value");
        }
        return std::move(m_value);
    }
    
    // 访问错误
    bool hasError() const { return !m_hasValue && m_error.hasError(); }
    
    const PipelineError& error() const {
        return m_error;
    }
    
    PipelineError& error() {
        return m_error;
    }
    
    // 静态工厂方法
    static Result success(T value) {
        return Result(std::move(value));
    }
    
    static Result error(PipelineError err) {
        return Result(std::move(err));
    }
    
private:
    bool m_hasValue;
    T m_value;
    PipelineError m_error;
};

// void 特化
template<>
class Result<void> {
public:
    Result() : m_success(true), m_error(PipelineError::success()) {}
    
    Result(PipelineError error) : m_success(!error.hasError()), 
                                   m_error(std::move(error)) {}
    
    // 移动
    Result(Result&& other) noexcept 
        : m_success(other.m_success),
          m_error(std::move(other.m_error)) {
        other.m_success = false;
    }
    
    Result& operator=(Result&& other) noexcept {
        if (this != &other) {
            m_success = other.m_success;
            m_error = std::move(other.m_error);
            other.m_success = false;
        }
        return *this;
    }
    
    // 删除拷贝
    Result(const Result&) = delete;
    Result& operator=(const Result&) = delete;
    
    // 布尔转换
    explicit operator bool() const { return m_success; }
    
    bool hasError() const { return !m_success; }
    
    const PipelineError& error() const { return m_error; }
    PipelineError& error() { return m_error; }
    
    static Result success() {
        return Result();
    }
    
    static Result error(PipelineError err) {
        return Result(std::move(err));
    }
    
private:
    bool m_success;
    PipelineError m_error;
};

// 输出运算符
inline std::ostream& operator<<(std::ostream& os, const PipelineError& error) {
    os << error.toString();
    return os;
}

// 辅助宏
#define RETURN_IF_ERROR(result) \
    do { \
        auto _res = (result); \
        if (!_res) return Result<void>::error(_res.error()); \
    } while(0)

} // namespace pipeline
