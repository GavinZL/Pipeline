/**
 * @file PipelineLog.h
 * @brief Pipeline日志系统
 * 
 * 仿照LREngine日志系统设计，提供统一的日志输出接口
 */

#pragma once

#include <functional>
#include <string>
#include <cstdarg>
#include <cstdint>

namespace pipeline {

/**
 * @brief 日志级别枚举
 */
enum class LogLevel : uint8_t {
    Trace = 0,    // 详细追踪（仅调试）
    Debug = 1,    // 调试信息（仅调试）
    Info = 2,     // 一般信息
    Warning = 3,  // 警告
    Error = 4,    // 错误
    Fatal = 5,    // 致命错误
    Off = 6       // 关闭日志
};

/**
 * @brief 日志条目结构
 */
struct LogEntry {
    LogLevel level;
    std::string message;
    std::string file;
    int32_t line;
    std::string function;
    uint64_t timestamp;    // 毫秒时间戳
    uint64_t threadId;
};

/**
 * @brief 日志回调函数类型
 */
using LogCallback = std::function<void(const LogEntry&)>;

/**
 * @brief Pipeline日志处理类
 * 
 * 静态类，提供全局日志功能
 */
class PipelineLog {
public:
    PipelineLog() = delete;  // 静态类，禁止实例化
    
    /**
     * @brief 初始化日志系统
     */
    static void initialize();
    
    /**
     * @brief 关闭日志系统
     */
    static void shutdown();
    
    /**
     * @brief 基础日志输出
     * @param level 日志级别
     * @param message 日志消息
     */
    static void log(LogLevel level, const char* message);
    
    /**
     * @brief 带位置信息的日志输出
     * @param level 日志级别
     * @param message 日志消息
     * @param file 源文件名
     * @param line 行号
     * @param function 函数名
     */
    static void logEx(LogLevel level, const char* message,
                      const char* file, int32_t line, const char* function);
    
    /**
     * @brief 格式化日志输出（printf 风格）
     * @param level 日志级别
     * @param file 源文件名
     * @param line 行号
     * @param function 函数名
     * @param format 格式字符串
     * @param ... 可变参数
     */
    static void logFormat(LogLevel level, const char* file, int32_t line, 
                          const char* function, const char* format, ...);
    
    /**
     * @brief 设置最低日志级别
     * @param level 最低级别（低于此级别的日志将被忽略）
     */
    static void setMinLevel(LogLevel level);
    
    /**
     * @brief 获取最低日志级别
     * @return 当前最低日志级别
     */
    static LogLevel getMinLevel();
    
    /**
     * @brief 启用/禁用控制台输出
     * @param enable 是否启用
     */
    static void enableConsoleOutput(bool enable);
    
    /**
     * @brief 启用文件输出
     * @param filepath 日志文件路径
     */
    static void enableFileOutput(const char* filepath);
    
    /**
     * @brief 禁用文件输出
     */
    static void disableFileOutput();
    
    /**
     * @brief 启用/禁用彩色输出
     * @param enable 是否启用
     */
    static void enableColorOutput(bool enable);
    
    /**
     * @brief 设置日志标签（用于区分不同模块）
     * @param tag 标签名称
     */
    static void setTag(const char* tag);
    
    /**
     * @brief 设置日志回调
     * @param callback 回调函数
     */
    static void setLogCallback(LogCallback callback);
    
    /**
     * @brief 刷新日志缓冲区
     */
    static void flush();
    
    /**
     * @brief 获取日志级别的字符串描述
     * @param level 日志级别
     * @return 级别描述字符串
     */
    static const char* getLevelString(LogLevel level);
    
private:
    /**
     * @brief 内部格式化实现
     */
    static void logFormatV(LogLevel level, const char* file, int32_t line,
                           const char* function, const char* format, va_list args);
};

// ============================================================================
// 格式化日志宏（printf 风格，支持可变参数）
// ============================================================================
#define PIPELINE_LOGT(fmt, ...)   pipeline::PipelineLog::logFormat(pipeline::LogLevel::Trace, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define PIPELINE_LOGD(fmt, ...)   pipeline::PipelineLog::logFormat(pipeline::LogLevel::Debug, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define PIPELINE_LOGI(fmt, ...)   pipeline::PipelineLog::logFormat(pipeline::LogLevel::Info, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define PIPELINE_LOGW(fmt, ...)   pipeline::PipelineLog::logFormat(pipeline::LogLevel::Warning, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define PIPELINE_LOGE(fmt, ...)   pipeline::PipelineLog::logFormat(pipeline::LogLevel::Error, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define PIPELINE_LOGF(fmt, ...)   pipeline::PipelineLog::logFormat(pipeline::LogLevel::Fatal, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)

// ============================================================================
// 条件编译：Release 模式下禁用 Trace/Debug
// ============================================================================
#if !defined(PIPELINE_DEBUG) && !defined(_DEBUG) && !defined(DEBUG)
    #undef PIPELINE_LOGT
    #undef PIPELINE_LOGD
    #define PIPELINE_LOGT(fmt, ...) ((void)0)
    #define PIPELINE_LOGD(fmt, ...) ((void)0)
#endif

} // namespace pipeline
