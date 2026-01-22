/**
 * @file PipelineConfig.h
 * @brief 管线配置和上下文
 */

#pragma once

#include "pipeline/data/EntityTypes.h"
#include <string>
#include <unordered_map>
#include <any>
#include <memory>
#include <mutex>

// 前向声明
namespace lrengine {
namespace render {
class LRRenderContext;
} // namespace render
} // namespace lrengine

namespace pipeline {

// 前向声明
class TexturePool;
class FramePacketPool;
class PipelineGraph;
class PipelineExecutor;

/**
 * @brief 管线配置
 */
struct PipelineConfig {
    // 名称
    std::string name = "Pipeline";
    
    // 渲染后端
    bool preferMetal = true;              // 优先使用Metal（macOS/iOS）
    bool preferGLES = true;               // 优先使用GLES（Android）
    
    // 资源池配置
    uint32_t texturePoolSize = 16;        // 纹理池大小
    uint32_t framePacketPoolSize = 5;     // 帧包池大小
    uint32_t bufferPoolSize = 8;          // 缓冲池大小
    
    // 执行配置
    uint32_t maxConcurrentFrames = 3;     // 最大并发帧数
    bool enableParallelExecution = true;  // 启用并行执行
    bool enableFrameSkipping = true;      // 启用跳帧
    
    // 调试配置
    bool enableProfiling = false;         // 启用性能分析
    bool enableValidation = true;         // 启用验证
    bool enableLogging = false;           // 启用日志
};

/**
 * @brief 管线上下文
 * 
 * 在管线执行过程中传递的上下文信息，包含：
 * - 渲染上下文
 * - 资源池
 * - 配置参数
 * - 帧级别的共享数据
 */
class PipelineContext {
public:
    PipelineContext();
    ~PipelineContext();
    
    // ==========================================================================
    // 渲染上下文
    // ==========================================================================
    
    /**
     * @brief 设置渲染上下文
     */
    void setRenderContext(lrengine::render::LRRenderContext* context);
    
    /**
     * @brief 获取渲染上下文
     */
    lrengine::render::LRRenderContext* getRenderContext() const { return mRenderContext; }
    
    // ==========================================================================
    // 资源池
    // ==========================================================================
    
    /**
     * @brief 设置纹理池
     */
    void setTexturePool(std::shared_ptr<TexturePool> pool);
    
    /**
     * @brief 获取纹理池
     */
    std::shared_ptr<TexturePool> getTexturePool() const { return mTexturePool; }
    
    /**
     * @brief 设置帧包池
     */
    void setFramePacketPool(std::shared_ptr<FramePacketPool> pool);
    
    /**
     * @brief 获取帧包池
     */
    std::shared_ptr<FramePacketPool> getFramePacketPool() const { return mFramePacketPool; }
    
    // ==========================================================================
    // 配置
    // ==========================================================================
    
    /**
     * @brief 设置配置
     */
    void setConfig(const PipelineConfig& config) { mConfig = config; }
    
    /**
     * @brief 获取配置
     */
    const PipelineConfig& getConfig() const { return mConfig; }
    
    // ==========================================================================
    // 帧数据
    // ==========================================================================
    
    /**
     * @brief 设置当前帧ID
     */
    void setCurrentFrameId(uint64_t frameId) { mCurrentFrameId = frameId; }
    
    /**
     * @brief 获取当前帧ID
     */
    uint64_t getCurrentFrameId() const { return mCurrentFrameId; }
    
    /**
     * @brief 设置当前时间戳
     */
    void setCurrentTimestamp(uint64_t timestamp) { mCurrentTimestamp = timestamp; }
    
    /**
     * @brief 获取当前时间戳
     */
    uint64_t getCurrentTimestamp() const { return mCurrentTimestamp; }
    
    // ==========================================================================
    // 共享数据
    // ==========================================================================
    
    /**
     * @brief 设置共享数据
     */
    template<typename T>
    void setSharedData(const std::string& key, T&& value) {
        std::lock_guard<std::mutex> lock(mSharedDataMutex);
        mSharedData[key] = std::forward<T>(value);
    }
    
    /**
     * @brief 获取共享数据
     */
    template<typename T>
    std::optional<T> getSharedData(const std::string& key) const {
        std::lock_guard<std::mutex> lock(mSharedDataMutex);
        auto it = mSharedData.find(key);
        if (it != mSharedData.end()) {
            try {
                return std::any_cast<T>(it->second);
            } catch (const std::bad_any_cast&) {
                return std::nullopt;
            }
        }
        return std::nullopt;
    }
    
    /**
     * @brief 检查共享数据是否存在
     */
    bool hasSharedData(const std::string& key) const;
    
    /**
     * @brief 移除共享数据
     */
    void removeSharedData(const std::string& key);
    
    /**
     * @brief 清除所有共享数据
     */
    void clearSharedData();
    
    // ==========================================================================
    // 性能统计
    // ==========================================================================
    
    /**
     * @brief 开始计时
     */
    void startTimer(const std::string& name);
    
    /**
     * @brief 停止计时并返回耗时（微秒）
     */
    uint64_t stopTimer(const std::string& name);
    
    /**
     * @brief 获取计时器值（微秒）
     */
    uint64_t getTimerValue(const std::string& name) const;
    
private:
    // 渲染上下文
    lrengine::render::LRRenderContext* mRenderContext = nullptr;
    
    // 资源池
    std::shared_ptr<TexturePool> mTexturePool;
    std::shared_ptr<FramePacketPool> mFramePacketPool;
    
    // 配置
    PipelineConfig mConfig;
    
    // 帧数据
    uint64_t mCurrentFrameId = 0;
    uint64_t mCurrentTimestamp = 0;
    
    // 共享数据
    mutable std::mutex mSharedDataMutex;
    std::unordered_map<std::string, std::any> mSharedData;
    
    // 计时器
    mutable std::mutex mTimerMutex;
    std::unordered_map<std::string, uint64_t> mTimerStarts;
    std::unordered_map<std::string, uint64_t> mTimerValues;
};

} // namespace pipeline
