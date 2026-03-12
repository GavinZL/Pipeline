#pragma once

#include "pipeline/PipelineNew.h"
#include "pipeline/core/PipelineError.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace pipeline {

/**
 * @brief JSON Pipeline 配置加载器
 * 
 * 支持从 JSON 文件/字符串加载 Pipeline 配置
 * 
 * JSON 结构示例：
 * @code
 * {
 *     "version": "2.0",
 *     "preset": "CameraPreview",
 *     "platform": {
 *         "type": "iOS"
 *     },
 *     "input": {
 *         "format": "RGBA",
 *         "width": 1920,
 *         "height": 1080
 *     },
 *     "output": {
 *         "resolution": {
 *             "width": 1920,
 *             "height": 1080
 *         }
 *     },
 *     "quality": "High",
 *     "filters": [
 *         {
 *             "type": "beauty",
 *             "params": {
 *                 "smooth": 0.7,
 *                 "whiten": 0.3
 *             }
 *         }
 *     ]
 * }
 * @endcode
 */
class PipelineConfigLoader {
public:
    /**
     * @brief 从 JSON 文件加载配置
     * @param filePath JSON 文件路径
     * @return Pipeline 实例或错误
     */
    static Result<std::shared_ptr<Pipeline>> fromFile(const std::string& filePath);
    
    /**
     * @brief 从 JSON 字符串加载配置
     * @param jsonString JSON 字符串
     * @return Pipeline 实例或错误
     */
    static Result<std::shared_ptr<Pipeline>> fromString(const std::string& jsonString);
    
    /**
     * @brief 验证 JSON 配置
     * @param json JSON 对象
     * @return 是否有效
     */
    static bool validate(const nlohmann::json& json);
    
    /**
     * @brief 获取验证错误信息
     * @return 最后验证错误
     */
    static std::string getLastValidationError();
    
private:
    static std::string s_lastValidationError;
    
    // 解析各个配置部分
    static Result<PipelinePreset> parsePreset(const nlohmann::json& json);
    static Result<PlatformType> parsePlatform(const nlohmann::json& json);
    static Result<input::InputFormat> parseInputFormat(const nlohmann::json& json);
    static Result<QualityLevel> parseQuality(const nlohmann::json& json);
    static Result<void> parseFilters(PipelineBuilder& builder, const nlohmann::json& json);
    static Result<void> parseOutputs(PipelineBuilder& builder, const nlohmann::json& json);
};

/**
 * @brief JSON Pipeline 配置导出器
 * 
 * 将 Pipeline 当前状态导出为 JSON 配置
 */
class PipelineConfigExporter {
public:
    /**
     * @brief 导出 Pipeline 配置到 JSON
     * @param pipeline Pipeline 实例
     * @return JSON 字符串或错误
     */
    static Result<std::string> toJson(const Pipeline& pipeline);
    
    /**
     * @brief 导出 Pipeline 配置到文件
     * @param pipeline Pipeline 实例
     * @param filePath 目标文件路径
     * @return 是否成功
     */
    static Result<void> toFile(const Pipeline& pipeline, const std::string& filePath);
    
    /**
     * @brief 导出 Pipeline 配置到 JSON 对象
     * @param pipeline Pipeline 实例
     * @return JSON 对象或错误
     */
    static Result<nlohmann::json> toJsonObject(const Pipeline& pipeline);
};

} // namespace pipeline
