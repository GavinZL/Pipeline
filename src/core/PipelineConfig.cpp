/**
 * @file PipelineConfig.cpp
 * @brief PipelineConfig和PipelineContext实现
 */

#include "pipeline/core/PipelineConfig.h"
#include "pipeline/pool/TexturePool.h"
#include "pipeline/pool/FramePacketPool.h"
#include <chrono>

namespace pipeline {

PipelineContext::PipelineContext() = default;

PipelineContext::~PipelineContext() = default;

// =============================================================================
// 渲染上下文
// =============================================================================

void PipelineContext::setRenderContext(lrengine::render::LRRenderContext* context) {
    mRenderContext = context;
}

// =============================================================================
// 资源池
// =============================================================================

void PipelineContext::setTexturePool(std::shared_ptr<TexturePool> pool) {
    mTexturePool = pool;
}

void PipelineContext::setFramePacketPool(std::shared_ptr<FramePacketPool> pool) {
    mFramePacketPool = pool;
}

// =============================================================================
// 共享数据
// =============================================================================

bool PipelineContext::hasSharedData(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mSharedDataMutex);
    return mSharedData.find(key) != mSharedData.end();
}

void PipelineContext::removeSharedData(const std::string& key) {
    std::lock_guard<std::mutex> lock(mSharedDataMutex);
    mSharedData.erase(key);
}

void PipelineContext::clearSharedData() {
    std::lock_guard<std::mutex> lock(mSharedDataMutex);
    mSharedData.clear();
}

// =============================================================================
// 性能统计
// =============================================================================

void PipelineContext::startTimer(const std::string& name) {
    std::lock_guard<std::mutex> lock(mTimerMutex);
    auto now = std::chrono::high_resolution_clock::now();
    mTimerStarts[name] = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();
}

uint64_t PipelineContext::stopTimer(const std::string& name) {
    std::lock_guard<std::mutex> lock(mTimerMutex);
    
    auto it = mTimerStarts.find(name);
    if (it == mTimerStarts.end()) {
        return 0;
    }
    
    auto now = std::chrono::high_resolution_clock::now();
    uint64_t endTime = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();
    
    uint64_t duration = endTime - it->second;
    mTimerValues[name] = duration;
    mTimerStarts.erase(it);
    
    return duration;
}

uint64_t PipelineContext::getTimerValue(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mTimerMutex);
    
    auto it = mTimerValues.find(name);
    if (it != mTimerValues.end()) {
        return it->second;
    }
    return 0;
}

} // namespace pipeline
