/**
 * @file OutputEntity.cpp
 * @brief OutputEntity 实现
 */

#include "pipeline/output/OutputEntity.h"
#include "pipeline/data/FramePacket.h"
#include "pipeline/core/PipelineConfig.h"

namespace pipeline {
namespace output {

// =============================================================================
// DisplayOutputTarget 实现
// =============================================================================

DisplayOutputTarget::DisplayOutputTarget(const std::string& name)
    : mName(name) {
}

DisplayOutputTarget::~DisplayOutputTarget() {
    release();
}

bool DisplayOutputTarget::initialize() {
    if (!mSurface) {
        return false;
    }
    return mSurface->isReady();
}

void DisplayOutputTarget::release() {
    if (mSurface) {
        mSurface->release();
    }
}

bool DisplayOutputTarget::output(const OutputData& data) {
    if (!mSurface || !mSurface->isReady()) {
        return false;
    }
    
    // 开始帧
    if (!mSurface->beginFrame()) {
        return false;
    }
    
    // 渲染纹理
    // TODO: 从 OutputData 获取纹理并渲染
    // mSurface->renderTexture(texture, mDisplayConfig);
    
    // 结束帧
    return mSurface->endFrame();
}

bool DisplayOutputTarget::isReady() const {
    return mSurface && mSurface->isReady();
}

void DisplayOutputTarget::setDisplaySurface(DisplaySurfacePtr surface) {
    mSurface = std::move(surface);
}

void DisplayOutputTarget::setDisplayConfig(const DisplayConfig& config) {
    mDisplayConfig = config;
    if (mSurface) {
        mSurface->setDisplayConfig(config);
    }
}

// =============================================================================
// CallbackOutputTarget 实现
// =============================================================================

CallbackOutputTarget::CallbackOutputTarget(const std::string& name)
    : mName(name) {
}

CallbackOutputTarget::~CallbackOutputTarget() = default;

bool CallbackOutputTarget::output(const OutputData& data) {
    bool success = true;
    
    // CPU 回调
    if (mCpuCallback && data.cpuData) {
        mCpuCallback(data.cpuData, data.cpuDataSize,
                     data.width, data.height,
                     data.format, data.timestamp);
    }
    
    // GPU 回调
    if (mGpuCallback && (data.textureId != 0 || data.metalTexture != nullptr)) {
        mGpuCallback(data.textureId, data.metalTexture,
                     data.width, data.height, data.timestamp);
    }
    
    return success;
}

void CallbackOutputTarget::setCPUCallback(CPUOutputCallback callback) {
    mCpuCallback = std::move(callback);
}

void CallbackOutputTarget::setGPUCallback(GPUOutputCallback callback) {
    mGpuCallback = std::move(callback);
}

// =============================================================================
// OutputEntity 实现
// =============================================================================

OutputEntity::OutputEntity(const std::string& name)
    : ProcessEntity(name) {
    initializePorts();
}

OutputEntity::~OutputEntity() {
    clearTargets();
}

// =============================================================================
// 端口初始化
// =============================================================================

void OutputEntity::initializePorts() {
    // 添加默认输入端口
    addInputPort(DEFAULT_INPUT_PORT);
}

// =============================================================================
// 配置
// =============================================================================

void OutputEntity::configure(const OutputConfig& config) {
    mConfig = config;
}

void OutputEntity::setDualInputMode(bool enabled) {
    if (mDualInputMode == enabled) {
        return;
    }
    
    mDualInputMode = enabled;
    
    if (enabled) {
        // 添加双路输入端口（如果不存在）
        if (!getInputPort(GPU_INPUT_PORT)) {
            addInputPort(GPU_INPUT_PORT);
        }
        if (!getInputPort(CPU_INPUT_PORT)) {
            addInputPort(CPU_INPUT_PORT);
        }
    }
}

// =============================================================================
// 输出目标管理
// =============================================================================

void OutputEntity::addTarget(OutputTargetPtr target) {
    if (!target) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(mTargetsMutex);
    
    // 检查是否已存在
    for (const auto& t : mTargets) {
        if (t->getName() == target->getName()) {
            return;
        }
    }
    
    // 初始化并添加
    target->initialize();
    mTargets.push_back(target);
}

void OutputEntity::removeTarget(const std::string& name) {
    std::lock_guard<std::mutex> lock(mTargetsMutex);
    
    auto it = std::remove_if(mTargets.begin(), mTargets.end(),
        [&name](const OutputTargetPtr& t) {
            return t->getName() == name;
        });
    
    if (it != mTargets.end()) {
        for (auto removeIt = it; removeIt != mTargets.end(); ++removeIt) {
            (*removeIt)->release();
        }
        mTargets.erase(it, mTargets.end());
    }
}

OutputTargetPtr OutputEntity::getTarget(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mTargetsMutex);
    
    for (const auto& t : mTargets) {
        if (t->getName() == name) {
            return t;
        }
    }
    return nullptr;
}

void OutputEntity::clearTargets() {
    std::lock_guard<std::mutex> lock(mTargetsMutex);
    
    for (auto& t : mTargets) {
        t->release();
    }
    mTargets.clear();
    mDisplayTarget.reset();
    mCallbackTarget.reset();
}

// =============================================================================
// 便捷方法
// =============================================================================

void OutputEntity::setDisplaySurface(DisplaySurfacePtr surface) {
    if (!mDisplayTarget) {
        mDisplayTarget = std::make_shared<DisplayOutputTarget>("display");
        addTarget(mDisplayTarget);
    }
    mDisplayTarget->setDisplaySurface(std::move(surface));
}

void OutputEntity::setCPUOutputCallback(CPUOutputCallback callback) {
    if (!mCallbackTarget) {
        mCallbackTarget = std::make_shared<CallbackOutputTarget>("callback");
        addTarget(mCallbackTarget);
    }
    mCallbackTarget->setCPUCallback(std::move(callback));
}

void OutputEntity::setGPUOutputCallback(GPUOutputCallback callback) {
    if (!mCallbackTarget) {
        mCallbackTarget = std::make_shared<CallbackOutputTarget>("callback");
        addTarget(mCallbackTarget);
    }
    mCallbackTarget->setGPUCallback(std::move(callback));
}

// =============================================================================
// ProcessEntity 生命周期
// =============================================================================

bool OutputEntity::prepare(PipelineContext& context) {
    std::lock_guard<std::mutex> lock(mTargetsMutex);
    
    // 初始化所有目标
    for (auto& target : mTargets) {
        if (!target->initialize()) {
            // 初始化失败，但继续尝试其他目标
        }
    }
    
    return true;
}

bool OutputEntity::process(const std::vector<FramePacketPtr>& inputs,
                           std::vector<FramePacketPtr>& outputs,
                           PipelineContext& context) {
    if (inputs.empty()) {
        return false;
    }
    
    // 处理输入
    for (const auto& packet : inputs) {
        if (packet) {
            processOutput(packet);
        }
    }
    
    // OutputEntity 不产生输出到下游
    return true;
}

void OutputEntity::finalize(PipelineContext& context) {
    // 输出完成后的清理
}

// =============================================================================
// 内部方法
// =============================================================================

bool OutputEntity::processOutput(FramePacketPtr packet) {
    if (!packet) {
        return false;
    }
    
    // 构建输出数据
    OutputData data;
    data.width = packet->getWidth();
    data.height = packet->getHeight();
    data.timestamp = packet->getTimestamp();
    data.frameId = packet->getFrameId();
    
    // CPU 数据
    const uint8_t* cpuBuffer = packet->getCpuBufferNoLoad();
    if (cpuBuffer) {
        data.cpuData = cpuBuffer;
        // data.cpuDataSize = packet->getCpuBufferSize();
    }
    
    // GPU 数据
    auto texture = packet->getTexture();
    if (texture) {
        // 获取纹理 ID（平台相关）
        // data.textureId = texture->getGLTextureId();
        // data.metalTexture = texture->getMetalTexture();
    }
    
    // 分发到所有目标
    dispatchToTargets(data);
    
    ++mOutputFrameCount;
    return true;
}

void OutputEntity::dispatchToTargets(const OutputData& data) {
    std::lock_guard<std::mutex> lock(mTargetsMutex);
    
    for (auto& target : mTargets) {
        if (target->isEnabled() && target->isReady()) {
            if (!target->output(data)) {
                // 输出失败，记录但继续
            }
        }
    }
}

} // namespace output
} // namespace pipeline
