#include "pipeline/platform/PlatformStrategy.h"
#include "PipelineImpl.h"
#include "pipeline/utils/PipelineLog.h"
#include <stdexcept>

namespace pipeline {
namespace strategy {

// ============================================================================
// GenericInputStrategy - 通用输入策略
// ============================================================================

class GenericInputStrategy : public InputStrategy {
public:
    bool initialize(PipelineImpl* impl, uint32_t width, uint32_t height) override {
        mImpl = impl;
        mWidth = width;
        mHeight = height;
        return true;
    }
    
    bool feedRGBA(const uint8_t* data, uint32_t width, uint32_t height, 
                  uint32_t stride, uint64_t timestamp) override {
        (void)data; (void)width; (void)height; (void)stride; (void)timestamp;
        PIPELINE_LOGI("GenericInputStrategy::feedRGBA");
        return true;
    }
    
    bool feedYUV420(const uint8_t* yData, const uint8_t* uData, 
                    const uint8_t* vData, uint32_t width, uint32_t height,
                    uint64_t timestamp) override {
        (void)yData; (void)uData; (void)vData; (void)width; (void)height; (void)timestamp;
        PIPELINE_LOGI("GenericInputStrategy::feedYUV420");
        return true;
    }
    
    bool feedNV12(const uint8_t* yData, const uint8_t* uvData,
                  uint32_t width, uint32_t height, bool isNV21,
                  uint64_t timestamp) override {
        (void)yData; (void)uvData; (void)width; (void)height; (void)isNV21; (void)timestamp;
        PIPELINE_LOGI("GenericInputStrategy::feedNV12");
        return true;
    }
    
    bool feedTexture(std::shared_ptr<::lrengine::render::LRTexture> texture, 
                     uint32_t width, uint32_t height, uint64_t timestamp) override {
        (void)texture; (void)width; (void)height; (void)timestamp;
        PIPELINE_LOGI("GenericInputStrategy::feedTexture");
        return true;
    }
    
    void destroy() override {
        mImpl = nullptr;
    }
    
    const char* getName() const override {
        return "GenericInputStrategy";
    }
    
private:
    PipelineImpl* mImpl = nullptr;
    uint32_t mWidth = 0;
    uint32_t mHeight = 0;
};

// ============================================================================
// GenericOutputStrategy - 通用输出策略
// ============================================================================

class GenericOutputStrategy : public OutputStrategy {
public:
    bool initialize(PipelineImpl* impl) override {
        mImpl = impl;
        return true;
    }
    
    int32_t createDisplayOutput(void* surface, uint32_t width, uint32_t height) override {
        (void)surface; (void)width; (void)height;
        PIPELINE_LOGI("GenericOutputStrategy::createDisplayOutput");
        static int32_t nextId = 1;
        return nextId++;
    }
    
    int32_t createCallbackOutput(
        std::function<void(const uint8_t*, size_t, uint32_t, uint32_t, int64_t)> callback,
        output::OutputFormat format) override {
        (void)callback; (void)format;
        PIPELINE_LOGI("GenericOutputStrategy::createCallbackOutput");
        static int32_t nextId = 1000;
        return nextId++;
    }
    
    bool removeOutput(int32_t targetId) override {
        (void)targetId;
        PIPELINE_LOGI("GenericOutputStrategy::removeOutput");
        return true;
    }
    
    void setOutputEnabled(int32_t targetId, bool enabled) override {
        (void)targetId; (void)enabled;
        PIPELINE_LOGI("GenericOutputStrategy::setOutputEnabled");
    }
    
    void destroy() override {
        mImpl = nullptr;
    }
    
    const char* getName() const override {
        return "GenericOutputStrategy";
    }
    
private:
    PipelineImpl* mImpl = nullptr;
};

// ============================================================================
// PlatformStrategyFactory 实现
// ============================================================================

std::unique_ptr<InputStrategy> PlatformStrategyFactory::createInputStrategy(
    PlatformType platform, 
    input::InputFormat format) {
    
    (void)format;
    
    switch (platform) {
        case PlatformType::Android:
            // TODO: 返回 AndroidInputStrategy
            PIPELINE_LOGI("Creating GenericInputStrategy for Android");
            return std::make_unique<GenericInputStrategy>();
            
        case PlatformType::iOS:
            // TODO: 返回 IOSInputStrategy
            PIPELINE_LOGI("Creating GenericInputStrategy for iOS");
            return std::make_unique<GenericInputStrategy>();
            
        case PlatformType::macOS:
            PIPELINE_LOGI("Creating GenericInputStrategy for macOS");
            return std::make_unique<GenericInputStrategy>();
            
        case PlatformType::Windows:
        case PlatformType::Linux:
        case PlatformType::Unknown:
        default:
            PIPELINE_LOGI("Creating GenericInputStrategy for generic platform");
            return std::make_unique<GenericInputStrategy>();
    }
}

std::unique_ptr<OutputStrategy> PlatformStrategyFactory::createOutputStrategy(PlatformType platform) {
    switch (platform) {
        case PlatformType::Android:
            PIPELINE_LOGI("Creating GenericOutputStrategy for Android");
            return std::make_unique<GenericOutputStrategy>();
            
        case PlatformType::iOS:
            PIPELINE_LOGI("Creating GenericOutputStrategy for iOS");
            return std::make_unique<GenericOutputStrategy>();
            
        case PlatformType::macOS:
            PIPELINE_LOGI("Creating GenericOutputStrategy for macOS");
            return std::make_unique<GenericOutputStrategy>();
            
        case PlatformType::Windows:
        case PlatformType::Linux:
        case PlatformType::Unknown:
        default:
            PIPELINE_LOGI("Creating GenericOutputStrategy for generic platform");
            return std::make_unique<GenericOutputStrategy>();
    }
}

} // namespace strategy
} // namespace pipeline
