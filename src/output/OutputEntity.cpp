/**
 * @file OutputEntity.cpp
 * @brief OutputEntity 实现
 */

#include "pipeline/output/OutputEntity.h"
#include "pipeline/data/FramePacket.h"
#include "pipeline/core/PipelineConfig.h"
#include "lrengine/core/LRPlanarTexture.h"
#include "lrengine/core/LRRenderContext.h"
#include "lrengine/core/LRTexture.h"
#include "lrengine/core/LRTypes.h"

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
    mCpuDataPlanarTexture.reset();
    if (mSurface) {
        mSurface->release();
    }
}

// 转换 OutputFormat 到 PlanarFormat
static lrengine::render::PlanarFormat toPlanarFormat(OutputFormat format) {
    switch (format) {
        case OutputFormat::YUV420: return lrengine::render::PlanarFormat::YUV420P;
        case OutputFormat::NV12:   return lrengine::render::PlanarFormat::NV12;
        case OutputFormat::NV21:   return lrengine::render::PlanarFormat::NV21;
        default:                   return lrengine::render::PlanarFormat::RGBA;
    }
}

std::shared_ptr<lrengine::render::LRPlanarTexture> DisplayOutputTarget::getOrCreateCpuPlanarTexture(
    lrengine::render::LRRenderContext* context,
    uint32_t width, uint32_t height,
    OutputFormat format) {
    
    // 检查是否需要重新创建纹理
    if (mCpuDataPlanarTexture && mCpuDataWidth == width && 
        mCpuDataHeight == height && mCpuDataFormat == format) {
        return mCpuDataPlanarTexture;
    }
    
    // 创建多平面纹理（支持所有格式）
    lrengine::render::PlanarTextureDescriptor desc;
    desc.width = width;
    desc.height = height;
    desc.format = toPlanarFormat(format);
    desc.debugName = "DisplayOutputTarget_CpuDataTexture";
    
    auto* rawTexture = context->CreatePlanarTexture(desc);
    if (!rawTexture) {
        return nullptr;
    }
    
    mCpuDataPlanarTexture = std::shared_ptr<lrengine::render::LRPlanarTexture>(
        rawTexture,
        [](lrengine::render::LRPlanarTexture* tex) {}
    );
    
    mCpuDataWidth = width;
    mCpuDataHeight = height;
    mCpuDataFormat = format;
    
    return mCpuDataPlanarTexture;
}

bool DisplayOutputTarget::output(const OutputData& data) {
    if (!mSurface || !mSurface->isReady()) {
        return false;
    }
    
    // 开始帧
    if (!mSurface->beginFrame()) {
        return false;
    }
    
    bool renderSuccess = false;
    
    // 优先使用 GPU 纹理数据
    if (data.planarTexture) {
        // 从 planarTexture 获取第一个平面渲染
        auto planeTexture = data.planarTexture->GetPlaneTexture(0);
        if (planeTexture) {
            auto texturePtr = std::shared_ptr<lrengine::render::LRTexture>(
                planeTexture, [](lrengine::render::LRTexture*) {});
            renderSuccess = mSurface->renderTexture(texturePtr, mDisplayConfig);
        }
    } else if (data.textureId != 0 || data.metalTexture != nullptr) {
        // 原始纹理句柄，需要转换（当前框架不支持，跳过）
        // TODO: 实现从原始句柄创建 LRTexture 的逻辑
    } else if (data.hasCpuData()) {
        // CPU 数据渲染：统一使用 PlanarTexture（支持所有格式）
        auto* context = mSurface->getRenderContext();
        if (context && data.width > 0 && data.height > 0) {
            auto planarTexture = getOrCreateCpuPlanarTexture(
                context, data.width, data.height, data.format);
            if (planarTexture) {
                // 根据 format 解析 CPU 数据并上传到各平面
                if (data.format == OutputFormat::YUV420) {
                    // YUV420P: 3平面 (Y, U, V)
                    uint32_t ySize = data.width * data.height;
                    uint32_t uvSize = ySize / 4;
                    const uint8_t* yData = data.cpuData;
                    const uint8_t* uData = yData + ySize;
                    const uint8_t* vData = uData + uvSize;
                    planarTexture->UpdateAllPlanes({yData, uData, vData});
                } else if (data.format == OutputFormat::NV12 || data.format == OutputFormat::NV21) {
                    // NV12/NV21: 2平面 (Y + UV)
                    uint32_t ySize = data.width * data.height;
                    const uint8_t* yData = data.cpuData;
                    const uint8_t* uvData = yData + ySize;
                    planarTexture->UpdateAllPlanes({yData, uvData});
                } else {
                    // RGBA/BGRA/RGB: 单平面
                    planarTexture->UpdateAllPlanes({data.cpuData});
                }
                // 渲染第一个平面
                auto planeTexture = planarTexture->GetPlaneTexture(0);
                if (planeTexture) {
                    auto texturePtr = std::shared_ptr<lrengine::render::LRTexture>(
                        planeTexture, [](lrengine::render::LRTexture*) {});
                    renderSuccess = mSurface->renderTexture(texturePtr, mDisplayConfig);
                }
            }
        }
    }
    
    // 结束帧
    return mSurface->endFrame() && renderSuccess;
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
    // 添加双路输入端口，对应 InputEntity 的双路输出
    addInputPort(GPU_INPUT_PORT);   // 接收 GPU 纹理数据
    addInputPort(CPU_INPUT_PORT);   // 接收 CPU 缓冲区数据
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
    
    // GPU 数据（统一使用 planarTexture）
    data.planarTexture = packet->getPlanarTexture();
    
    // CPU 数据
    const uint8_t* cpuBuffer = packet->getCpuBufferNoLoad();
    if (cpuBuffer) {
        data.cpuData = cpuBuffer;
        // 计算 CPU 数据大小
        uint32_t stride = packet->getStride();
        if (stride == 0) {
            stride = data.width * 4;  // 假设 RGBA
        }
        data.cpuDataSize = stride * data.height;
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
