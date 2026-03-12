#include "pipeline/core/PipelineConfigJson.h"
#include <fstream>
#include <iostream>
#include <map>

namespace pipeline {

std::string PipelineConfigLoader::s_lastValidationError;

// ============================================================================
// PipelineConfigLoader 实现
// ============================================================================

Result<std::shared_ptr<Pipeline>> PipelineConfigLoader::fromFile(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        return Result<std::shared_ptr<Pipeline>>::error(
            PipelineError::resourceError("Failed to open config file: " + filePath));
    }
    
    try {
        nlohmann::json json;
        file >> json;
        return fromString(json.dump());
    } catch (const std::exception& e) {
        return Result<std::shared_ptr<Pipeline>>::error(
            PipelineError::invalidArgument("Failed to parse JSON: " + std::string(e.what())));
    }
}

Result<std::shared_ptr<Pipeline>> PipelineConfigLoader::fromString(const std::string& jsonString) {
    try {
        nlohmann::json json = nlohmann::json::parse(jsonString);
        
        if (!validate(json)) {
            return Result<std::shared_ptr<Pipeline>>::error(
                PipelineError::invalidArgument("Invalid JSON config: " + s_lastValidationError));
        }
        
        // 创建 Builder
        PipelineBuilder builder;
        
        // 解析 Preset
        auto presetResult = parsePreset(json);
        if (!presetResult) {
            return Result<std::shared_ptr<Pipeline>>::error(presetResult.error());
        }
        builder.withPreset(presetResult.value());
        
        // 解析 Platform
        if (json.contains("platform") && json["platform"].contains("type")) {
            auto platformResult = parsePlatform(json);
            if (platformResult) {
                builder.withPlatform(platformResult.value());
            }
        }
        
        // 解析 Input
        if (json.contains("input")) {
            auto inputFormatResult = parseInputFormat(json);
            if (inputFormatResult) {
                auto format = inputFormatResult.value();
                uint32_t width = json["input"].value("width", 1920);
                uint32_t height = json["input"].value("height", 1080);
                
                switch (format) {
                    case input::InputFormat::RGBA:
                        builder.withRGBAInput(width, height);
                        break;
                    case input::InputFormat::YUV420:
                        builder.withYUVInput(width, height);
                        break;
                    case input::InputFormat::NV12:
                        builder.withNV12Input(width, height);
                        break;
                }
            }
        }
        
        // 解析 Quality
        if (json.contains("quality")) {
            auto qualityResult = parseQuality(json);
            if (qualityResult) {
                builder.withQuality(qualityResult.value());
            }
        }
        
        // 解析 Resolution
        if (json.contains("output") && json["output"].contains("resolution")) {
            uint32_t width = json["output"]["resolution"].value("width", 1920);
            uint32_t height = json["output"]["resolution"].value("height", 1080);
            builder.withResolution(width, height);
        }
        
        // 解析 Filters
        if (json.contains("filters")) {
            auto filterResult = parseFilters(builder, json);
            if (!filterResult) {
                return Result<std::shared_ptr<Pipeline>>::error(filterResult.error());
            }
        }
        
        // 构建 Pipeline
        auto pipeline = builder.build();
        if (!pipeline) {
            return Result<std::shared_ptr<Pipeline>>::error(
                PipelineError::initializationFailed("Failed to build pipeline from config"));
        }
        
        return Result<std::shared_ptr<Pipeline>>::success(pipeline);
        
    } catch (const std::exception& e) {
        return Result<std::shared_ptr<Pipeline>>::error(
            PipelineError::invalidArgument("JSON parse error: " + std::string(e.what())));
    }
}

bool PipelineConfigLoader::validate(const nlohmann::json& json) {
    // 检查版本
    if (!json.contains("version")) {
        s_lastValidationError = "Missing required field: version";
        return false;
    }
    
    // 检查 version 是否为字符串
    if (!json["version"].is_string()) {
        s_lastValidationError = "Field 'version' must be a string";
        return false;
    }
    
    // 检查 Preset（可选但如果有必须有效）
    if (json.contains("preset")) {
        auto presetResult = parsePreset(json);
        if (!presetResult) {
            s_lastValidationError = presetResult.error().message();
            return false;
        }
    }
    
    // 检查 Platform（可选但如果有必须有效）
    if (json.contains("platform")) {
        if (!json["platform"].is_object()) {
            s_lastValidationError = "Field 'platform' must be an object";
            return false;
        }
        if (json["platform"].contains("type")) {
            auto platformResult = parsePlatform(json);
            if (!platformResult) {
                s_lastValidationError = platformResult.error().message();
                return false;
            }
        }
    }
    
    return true;
}

std::string PipelineConfigLoader::getLastValidationError() {
    return s_lastValidationError;
}

// ============================================================================
// 解析辅助函数
// ============================================================================

Result<PipelinePreset> PipelineConfigLoader::parsePreset(const nlohmann::json& json) {
    static const std::map<std::string, PipelinePreset> presetMap = {
        {"CameraPreview", PipelinePreset::CameraPreview},
        {"CameraRecord", PipelinePreset::CameraRecord},
        {"ImageProcess", PipelinePreset::ImageProcess},
        {"LiveStream", PipelinePreset::LiveStream},
        {"VideoPlayback", PipelinePreset::VideoPlayback},
        {"Custom", PipelinePreset::Custom}
    };
    
    if (!json.contains("preset")) {
        return Result<PipelinePreset>::error(
            PipelineError::invalidArgument("Missing preset field"));
    }
    
    std::string presetStr = json["preset"].get<std::string>();
    auto it = presetMap.find(presetStr);
    if (it == presetMap.end()) {
        return Result<PipelinePreset>::error(
            PipelineError::invalidArgument("Unknown preset: " + presetStr));
    }
    
    return Result<PipelinePreset>::success(it->second);
}

Result<PlatformType> PipelineConfigLoader::parsePlatform(const nlohmann::json& json) {
    static const std::map<std::string, PlatformType> platformMap = {
        {"iOS", PlatformType::iOS},
        {"Android", PlatformType::Android},
        {"macOS", PlatformType::macOS},
        {"Windows", PlatformType::Windows},
        {"Linux", PlatformType::Linux}
    };
    
    if (!json.contains("platform") || !json["platform"].contains("type")) {
        return Result<PlatformType>::error(
            PipelineError::invalidArgument("Missing platform.type field"));
    }
    
    std::string platformStr = json["platform"]["type"].get<std::string>();
    auto it = platformMap.find(platformStr);
    if (it == platformMap.end()) {
        return Result<PlatformType>::error(
            PipelineError::invalidArgument("Unknown platform: " + platformStr));
    }
    
    return Result<PlatformType>::success(it->second);
}

Result<input::InputFormat> PipelineConfigLoader::parseInputFormat(const nlohmann::json& json) {
    static const std::map<std::string, input::InputFormat> formatMap = {
        {"RGBA", input::InputFormat::RGBA},
        {"YUV420", input::InputFormat::YUV420},
        {"NV12", input::InputFormat::NV12}
    };
    
    if (!json.contains("input") || !json["input"].contains("format")) {
        return Result<input::InputFormat>::error(
            PipelineError::invalidArgument("Missing input.format field"));
    }
    
    std::string formatStr = json["input"]["format"].get<std::string>();
    auto it = formatMap.find(formatStr);
    if (it == formatMap.end()) {
        return Result<input::InputFormat>::error(
            PipelineError::invalidArgument("Unknown input format: " + formatStr));
    }
    
    return Result<input::InputFormat>::success(it->second);
}

Result<QualityLevel> PipelineConfigLoader::parseQuality(const nlohmann::json& json) {
    static const std::map<std::string, QualityLevel> qualityMap = {
        {"Low", QualityLevel::Low},
        {"Medium", QualityLevel::Medium},
        {"High", QualityLevel::High},
        {"Ultra", QualityLevel::Ultra}
    };
    
    if (!json.contains("quality")) {
        return Result<QualityLevel>::error(
            PipelineError::invalidArgument("Missing quality field"));
    }
    
    std::string qualityStr = json["quality"].get<std::string>();
    auto it = qualityMap.find(qualityStr);
    if (it == qualityMap.end()) {
        return Result<QualityLevel>::error(
            PipelineError::invalidArgument("Unknown quality: " + qualityStr));
    }
    
    return Result<QualityLevel>::success(it->second);
}

Result<void> PipelineConfigLoader::parseFilters(PipelineBuilder& builder, const nlohmann::json& json) {
    if (!json.contains("filters") || !json["filters"].is_array()) {
        return Result<void>::success();
    }
    
    for (const auto& filter : json["filters"]) {
        if (!filter.contains("type")) {
            return Result<void>::error(
                PipelineError::invalidArgument("Filter missing 'type' field"));
        }
        
        std::string type = filter["type"].get<std::string>();
        
        if (type == "beauty" && filter.contains("params")) {
            float smooth = filter["params"].value("smooth", 0.5f);
            float whiten = filter["params"].value("whiten", 0.5f);
            builder.withBeautyFilter(smooth, whiten);
        } else if (type == "color" && filter.contains("params")) {
            std::string name = filter["params"].value("name", "vintage");
            float intensity = filter["params"].value("intensity", 1.0f);
            builder.withColorFilter(name, intensity);
        } else if (type == "sharpen" && filter.contains("params")) {
            float amount = filter["params"].value("amount", 0.5f);
            builder.withSharpenFilter(amount);
        } else if (type == "blur" && filter.contains("params")) {
            float radius = filter["params"].value("radius", 5.0f);
            builder.withBlurFilter(radius);
        }
    }
    
    return Result<void>::success();
}

Result<void> PipelineConfigLoader::parseOutputs(PipelineBuilder& builder, const nlohmann::json& json) {
    // TODO: 实现输出解析
    (void)builder;
    (void)json;
    return Result<void>::success();
}

// ============================================================================
// PipelineConfigExporter 实现
// ============================================================================

Result<std::string> PipelineConfigExporter::toJson(const Pipeline& pipeline) {
    auto jsonResult = toJsonObject(pipeline);
    if (!jsonResult) {
        return Result<std::string>::error(jsonResult.error());
    }
    
    try {
        return Result<std::string>::success(jsonResult.value().dump(2));
    } catch (const std::exception& e) {
        return Result<std::string>::error(
            PipelineError::internalError("Failed to serialize JSON: " + std::string(e.what())));
    }
}

Result<void> PipelineConfigExporter::toFile(const Pipeline& pipeline, const std::string& filePath) {
    auto jsonResult = toJson(pipeline);
    if (!jsonResult) {
        return Result<void>::error(jsonResult.error());
    }
    
    std::ofstream file(filePath);
    if (!file.is_open()) {
        return Result<void>::error(
            PipelineError::resourceError("Failed to open file for writing: " + filePath));
    }
    
    file << jsonResult.value();
    return Result<void>::success();
}

Result<nlohmann::json> PipelineConfigExporter::toJsonObject(const Pipeline& pipeline) {
    (void)pipeline;
    nlohmann::json json;
    
    try {
        json["version"] = "2.0";
        json["preset"] = "Custom";
        json["platform"] = {{"type", "iOS"}};
        json["input"] = {{"format", "RGBA"}, {"width", 1920}, {"height", 1080}};
        json["output"] = {{"resolution", {{"width", 1920}, {"height", 1080}}}};
        json["quality"] = "High";
        json["filters"] = nlohmann::json::array();
        
        return Result<nlohmann::json>::success(json);
        
    } catch (const std::exception& e) {
        return Result<nlohmann::json>::error(
            PipelineError::internalError("Failed to create JSON object: " + std::string(e.what())));
    }
}

} // namespace pipeline
