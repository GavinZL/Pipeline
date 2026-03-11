#include "pipeline/PipelineNew.h"
#include "PipelineImpl.h"
#include <stdexcept>

namespace pipeline {

// ============================================================================
// Pipeline 工厂方法
// ============================================================================

PipelineBuilder Pipeline::create() {
    return PipelineBuilder();
}

std::shared_ptr<Pipeline> Pipeline::fromJsonFile(const std::string& configFilePath) {
    (void)configFilePath;
    // Phase 4: JSON 配置支持
    return nullptr;
}

// ============================================================================
// 构造/析构
// ============================================================================

Pipeline::Pipeline(std::unique_ptr<PipelineImpl> impl) 
    : mImpl(std::move(impl)) {
}

Pipeline::~Pipeline() = default;

// ============================================================================
// 生命周期
// ============================================================================

bool Pipeline::start() {
    return mImpl ? mImpl->start() : false;
}

void Pipeline::pause() {
    if (mImpl) mImpl->pause();
}

void Pipeline::resume() {
    if (mImpl) mImpl->resume();
}

void Pipeline::stop() {
    if (mImpl) mImpl->stop();
}

void Pipeline::destroy() {
    if (mImpl) mImpl->destroy();
}

PipelineState Pipeline::getState() const {
    return mImpl ? mImpl->getState() : PipelineState::Created;
}

bool Pipeline::isRunning() const {
    return mImpl ? mImpl->isRunning() : false;
}

// ============================================================================
// 输入接口
// ============================================================================

bool Pipeline::feedRGBA(const uint8_t* data, uint32_t width, uint32_t height, 
                        uint32_t stride, uint64_t timestamp) {
    return mImpl ? mImpl->feedRGBA(data, width, height, stride, timestamp) : false;
}

bool Pipeline::feedYUV420(const uint8_t* yData, const uint8_t* uData, 
                          const uint8_t* vData, uint32_t width, uint32_t height,
                          uint64_t timestamp) {
    return mImpl ? mImpl->feedYUV420(yData, uData, vData, width, height, timestamp) : false;
}

bool Pipeline::feedNV12(const uint8_t* yData, const uint8_t* uvData,
                        uint32_t width, uint32_t height, bool isNV21,
                        uint64_t timestamp) {
    return mImpl ? mImpl->feedNV12(yData, uvData, width, height, isNV21, timestamp) : false;
}

bool Pipeline::feedTexture(std::shared_ptr<::lrengine::render::LRTexture> texture, 
                           uint32_t width, uint32_t height, uint64_t timestamp) {
    return mImpl ? mImpl->feedTexture(std::move(texture), width, height, timestamp) : false;
}

// ============================================================================
// Entity 管理
// ============================================================================

EntityId Pipeline::addEntity(ProcessEntityPtr entity) {
    return mImpl ? mImpl->addEntity(std::move(entity)) : InvalidEntityId;
}

bool Pipeline::removeEntity(EntityId entityId) {
    return mImpl ? mImpl->removeEntity(entityId) : false;
}

void Pipeline::setEntityEnabled(EntityId entityId, bool enabled) {
    if (mImpl) mImpl->setEntityEnabled(entityId, enabled);
}

EntityId Pipeline::addBeautyFilter(float smoothLevel, float whitenLevel) {
    return mImpl ? mImpl->addBeautyFilter(smoothLevel, whitenLevel) : InvalidEntityId;
}

EntityId Pipeline::addColorFilter(const std::string& filterName, float intensity) {
    return mImpl ? mImpl->addColorFilter(filterName, intensity) : InvalidEntityId;
}

EntityId Pipeline::addSharpenFilter(float amount) {
    return mImpl ? mImpl->addSharpenFilter(amount) : InvalidEntityId;
}

// ============================================================================
// 输出管理
// ============================================================================

int32_t Pipeline::addDisplayOutput(void* surface, uint32_t width, uint32_t height) {
    return mImpl ? mImpl->addDisplayOutput(surface, width, height) : -1;
}

int32_t Pipeline::addCallbackOutput(
    std::function<void(const uint8_t*, size_t, uint32_t, uint32_t, int64_t)> callback,
    output::OutputFormat format) {
    return mImpl ? mImpl->addCallbackOutput(std::move(callback), format) : -1;
}

bool Pipeline::removeOutputTarget(int32_t targetId) {
    return mImpl ? mImpl->removeOutputTarget(targetId) : false;
}

void Pipeline::setOutputTargetEnabled(int32_t targetId, bool enabled) {
    if (mImpl) mImpl->setOutputTargetEnabled(targetId, enabled);
}

// ============================================================================
// 配置
// ============================================================================

void Pipeline::setOutputResolution(uint32_t width, uint32_t height) {
    if (mImpl) mImpl->setOutputResolution(width, height);
}

void Pipeline::setRotation(int32_t degrees) {
    if (mImpl) mImpl->setRotation(degrees);
}

void Pipeline::setMirror(bool horizontal, bool vertical) {
    if (mImpl) mImpl->setMirror(horizontal, vertical);
}

void Pipeline::setFrameRateLimit(int32_t fps) {
    if (mImpl) mImpl->setFrameRateLimit(fps);
}

// ============================================================================
// 回调
// ============================================================================

void Pipeline::setFrameProcessedCallback(std::function<void(FramePacketPtr)> callback) {
    if (mImpl) mImpl->setFrameProcessedCallback(std::move(callback));
}

void Pipeline::setErrorCallback(std::function<void(const std::string&)> callback) {
    if (mImpl) mImpl->setErrorCallback(std::move(callback));
}

void Pipeline::setStateCallback(std::function<void(PipelineState)> callback) {
    if (mImpl) mImpl->setStateCallback(std::move(callback));
}

// ============================================================================
// 统计
// ============================================================================

ExecutionStats Pipeline::getStats() const {
    return mImpl ? mImpl->getStats() : ExecutionStats{};
}

double Pipeline::getAverageProcessTime() const {
    return mImpl ? mImpl->getAverageProcessTime() : 0.0;
}

bool Pipeline::exportConfig(const std::string& filePath) const {
    (void)filePath;
    // Phase 4: JSON 导出
    return false;
}

std::string Pipeline::exportGraph() const {
    return mImpl ? mImpl->exportGraph() : "";
}

} // namespace pipeline
