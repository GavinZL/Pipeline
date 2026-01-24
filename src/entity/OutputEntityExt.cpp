/**
 * @file OutputEntityExt.cpp
 * @brief 扩展输出实体实现
 * 
 * Phase 2: 多输出目标具体处理逻辑实现
 */

#include "pipeline/entity/OutputEntityExt.h"
#include "pipeline/data/FramePacket.h"

// LREngine headers
#include <lrengine/core/LRRenderContext.h>
#include <lrengine/core/LRShader.h>
#include <lrengine/core/LRTexture.h>
#include <lrengine/core/LRFrameBuffer.h>
#include <lrengine/core/LRBuffer.h>
#include <lrengine/core/LRTypes.h>

#include <cstring>
#include <chrono>

namespace pipeline {

namespace {

// 显示/输出着色器（与 OutputEntity 保持一致）
const char* kDisplayVertexShaderExt = R"(
attribute vec4 aPosition;
attribute vec2 aTexCoord;
varying vec2 vTexCoord;
uniform mat4 uMVPMatrix;

void main() {
    gl_Position = uMVPMatrix * aPosition;
    vTexCoord = aTexCoord;
}
)";

const char* kDisplayFragmentShaderExt = R"(
precision mediump float;
varying vec2 vTexCoord;
uniform sampler2D uTexture;

void main() {
    gl_FragColor = texture2D(uTexture, vTexCoord);
}
)";

// 全屏四边形顶点数据 (position.xy, texcoord.xy)
const float kQuadVerticesExt[] = {
    // position     texcoord
    -1.0f, -1.0f,   0.0f, 0.0f,  // 左下
     1.0f, -1.0f,   1.0f, 0.0f,  // 右下
    -1.0f,  1.0f,   0.0f, 1.0f,  // 左上
     1.0f,  1.0f,   1.0f, 1.0f,  // 右上
};

} // anonymous namespace

OutputEntityExt::OutputEntityExt(const std::string& name)
    : ProcessEntity(name)
    , mRenderContext(nullptr)
    , mPlatformContext(nullptr)
    , mNextTargetId(1)
    , mIsRunning(false)
    , mIsPaused(false)
{
}

OutputEntityExt::~OutputEntityExt() {
    stop();
}

void OutputEntityExt::setRenderContext(lrengine::render::LRRenderContext* context) {
    mRenderContext = context;
}

void OutputEntityExt::setPlatformContext(PlatformContext* platformContext) {
    mPlatformContext = platformContext;
}

int32_t OutputEntityExt::addOutputTarget(const OutputConfig& config) {
    std::lock_guard<std::mutex> lock(mTargetsMutex);
    
    int32_t targetId = mNextTargetId++;
    mOutputTargets[targetId] = config;
    
    return targetId;
}

bool OutputEntityExt::removeOutputTarget(int32_t targetId) {
    std::lock_guard<std::mutex> lock(mTargetsMutex);
    
    auto it = mOutputTargets.find(targetId);
    if (it == mOutputTargets.end()) {
        return false;
    }
    
    mOutputTargets.erase(it);
    return true;
}

bool OutputEntityExt::updateOutputTarget(int32_t targetId, const OutputConfig& config) {
    std::lock_guard<std::mutex> lock(mTargetsMutex);
    
    auto it = mOutputTargets.find(targetId);
    if (it == mOutputTargets.end()) {
        return false;
    }
    
    it->second = config;
    return true;
}

void OutputEntityExt::setOutputTargetEnabled(int32_t targetId, bool enabled) {
    std::lock_guard<std::mutex> lock(mTargetsMutex);
    
    auto it = mOutputTargets.find(targetId);
    if (it != mOutputTargets.end()) {
        it->second.enabled = enabled;
    }
}

std::vector<int32_t> OutputEntityExt::getOutputTargets() const {
    std::lock_guard<std::mutex> lock(mTargetsMutex);
    
    std::vector<int32_t> targets;
    targets.reserve(mOutputTargets.size());
    
    for (const auto& [id, config] : mOutputTargets) {
        targets.push_back(id);
    }
    
    return targets;
}

void OutputEntityExt::clearOutputTargets() {
    std::lock_guard<std::mutex> lock(mTargetsMutex);
    mOutputTargets.clear();
}

// 快捷配置方法（占位实现）
int32_t OutputEntityExt::setupDisplayOutput(void* surface, int32_t width, int32_t height) {
    OutputConfig config;
    config.targetType = OutputTargetType::Display;
    config.displayConfig.surface = surface;
    config.displayConfig.width = width;
    config.displayConfig.height = height;
    
    return addOutputTarget(config);
}

int32_t OutputEntityExt::setupEncoderOutput(void* encoderSurface, EncoderType encoderType) {
    OutputConfig config;
    config.targetType = OutputTargetType::Encoder;
    config.encoderConfig.encoderSurface = encoderSurface;
    config.encoderConfig.encoderType = encoderType;
    
    return addOutputTarget(config);
}

int32_t OutputEntityExt::setupCallbackOutput(FrameCallback callback, OutputDataFormat dataFormat) {
    OutputConfig config;
    config.targetType = OutputTargetType::Callback;
    config.callbackConfig.frameCallback = callback;
    config.callbackConfig.dataFormat = dataFormat;
    
    return addOutputTarget(config);
}

int32_t OutputEntityExt::setupTextureOutput(bool shareTexture) {
    OutputConfig config;
    config.targetType = OutputTargetType::Texture;
    config.textureConfig.shareTexture = shareTexture;
    
    return addOutputTarget(config);
}

int32_t OutputEntityExt::setupFileOutput(const std::string& filePath, const std::string& fileFormat) {
    OutputConfig config;
    config.targetType = OutputTargetType::File;
    config.fileConfig.filePath = filePath;
    config.fileConfig.fileFormat = fileFormat;
    
    return addOutputTarget(config);
}

bool OutputEntityExt::start() {
    mIsRunning = true;
    mIsPaused = false;
    return true;
}

void OutputEntityExt::stop() {
    mIsRunning = false;
    mIsPaused = false;
}

void OutputEntityExt::pause() {
    mIsPaused = true;
}

void OutputEntityExt::resume() {
    mIsPaused = false;
}

std::shared_ptr<lrengine::render::LRTexture> OutputEntityExt::getOutputTexture() const {
    std::lock_guard<std::mutex> lock(mLastOutputMutex);
    if (mLastOutput) {
        return mLastOutput->getTexture();
    }
    return nullptr;
}

FramePacketPtr OutputEntityExt::getLastOutput() const {
    std::lock_guard<std::mutex> lock(mLastOutputMutex);
    return mLastOutput;
}

size_t OutputEntityExt::readPixels(uint8_t* buffer, size_t bufferSize, OutputDataFormat dataFormat) {
    if (!buffer || bufferSize == 0) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(mLastOutputMutex);
    if (!mLastOutput) {
        return 0;
    }

    uint32_t width = mLastOutput->getWidth();
    uint32_t height = mLastOutput->getHeight();
    if (width == 0 || height == 0) {
        return 0;
    }

    // 目前仅支持基于CPU缓冲区的读取
    const uint8_t* src = mLastOutput->getCpuBuffer();
    if (!src) {
        return 0;
    }

    size_t requiredSize = static_cast<size_t>(width) * height * 4; // 仅支持RGBA/BGRA
    if (bufferSize < requiredSize) {
        return 0;
    }

    // 根据目标格式进行简单转换
    if (!convertFormat(mLastOutput, dataFormat, buffer)) {
        return 0;
    }

    return requiredSize;
}

OutputEntityExt::OutputStats OutputEntityExt::getStats() const {
    std::lock_guard<std::mutex> lock(mStatsMutex);
    return mStats;
}

void OutputEntityExt::resetStats() {
    std::lock_guard<std::mutex> lock(mStatsMutex);
    mStats = OutputStats();
}

bool OutputEntityExt::process(const std::vector<FramePacketPtr>& inputs,
                              std::vector<FramePacketPtr>& outputs,
                              PipelineContext& context) {
    if (inputs.empty() || !inputs[0]) {
        return false;
    }

    FramePacketPtr input = inputs[0];

    // 更新最后输出
    {
        std::lock_guard<std::mutex> lock(mLastOutputMutex);
        mLastOutput = input;
    }

    if (!mIsRunning || mIsPaused) {
        // 不中断管线，只是不做实际输出
        outputs.push_back(input);
        return true;
    }

    // 拷贝当前输出目标，避免长时间持有锁
    std::unordered_map<int32_t, OutputConfig> targetsCopy;
    {
        std::lock_guard<std::mutex> lock(mTargetsMutex);
        targetsCopy = mOutputTargets;
    }

    bool allSuccess = true;

    for (const auto& [id, config] : targetsCopy) {
        if (!config.enabled) {
            continue;
        }

        bool ok = processOutputTarget(config, input);
        if (!ok) {
            allSuccess = false;
        }

        // 更新按目标统计
        {
            std::lock_guard<std::mutex> statsLock(mStatsMutex);
            mStats.targetFrameCounts[id]++;
            if (!ok) {
                mStats.errorFrames++;
            }
        }
    }

    // 更新全局统计（帧率/延迟）
    {
        auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> statsLock(mStatsMutex);

        mStats.totalFrames++;

        if (mLastStatsTime.time_since_epoch().count() != 0) {
            auto dtMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - mLastStatsTime).count();
            if (dtMs > 0) {
                double seconds = static_cast<double>(dtMs) / 1000.0;
                mStats.averageFPS = static_cast<double>(mStats.totalFrames) / seconds;
            }
        }
        mLastStatsTime = now;

        // 简单的平均延迟估算（假设时间戳为毫秒）
        uint64_t ts = input->getTimestamp();
        if (ts != 0) {
            uint64_t nowMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
            double latency = static_cast<double>(nowMs > ts ? nowMs - ts : 0);
            if (mStats.totalFrames == 1) {
                mStats.averageLatency = latency;
            } else {
                mStats.averageLatency += (latency - mStats.averageLatency) / static_cast<double>(mStats.totalFrames);
            }
        }
    }

    // OutputEntityExt 通常也不改变数据包，继续传递给下游
    outputs.push_back(input);
    return allSuccess;
}

bool OutputEntityExt::processOutputTarget(const OutputConfig& config, FramePacketPtr input) {
    switch (config.targetType) {
        case OutputTargetType::Display:
            return renderToDisplay(config.displayConfig, input);
        case OutputTargetType::Encoder:
            return outputToEncoder(config.encoderConfig, input);
        case OutputTargetType::Callback:
            return executeCallback(config.callbackConfig, input);
        case OutputTargetType::Texture:
            return outputTexture(config.textureConfig, input);
        case OutputTargetType::File:
            return saveToFile(config.fileConfig, input);
        case OutputTargetType::PixelBuffer:
        case OutputTargetType::SurfaceTexture:
            return outputToPlatform(config.platformConfig, input);
        case OutputTargetType::Custom:
            if (config.customOutputFunc) {
                return config.customOutputFunc(input);
            }
            return false;
        default:
            return false;
    }
}

bool OutputEntityExt::renderToDisplay(const DisplayOutputConfig& config, FramePacketPtr input) {
    if (!mRenderContext) {
        return false;
    }

    auto texture = input ? input->getTexture() : nullptr;
    if (!texture) {
        return false;
    }

    using namespace lrengine::render;

    // 创建显示着色器
    if (!mDisplayShader) {
        ShaderDescriptor vsDesc;
        vsDesc.stage = ShaderStage::Vertex;
        vsDesc.language = ShaderLanguage::GLSL;
        vsDesc.source = kDisplayVertexShaderExt;
        vsDesc.debugName = "OutputExtDisplayVS";

        LRShader* vertexShader = mRenderContext->CreateShader(vsDesc);
        if (!vertexShader || !vertexShader->IsCompiled()) {
            return false;
        }

        ShaderDescriptor fsDesc;
        fsDesc.stage = ShaderStage::Fragment;
        fsDesc.language = ShaderLanguage::GLSL;
        fsDesc.source = kDisplayFragmentShaderExt;
        fsDesc.debugName = "OutputExtDisplayFS";

        LRShader* fragmentShader = mRenderContext->CreateShader(fsDesc);
        if (!fragmentShader || !fragmentShader->IsCompiled()) {
            delete vertexShader;
            return false;
        }

        LRShaderProgram* program = mRenderContext->CreateShaderProgram(vertexShader, fragmentShader);
        if (!program || !program->IsLinked()) {
            delete vertexShader;
            delete fragmentShader;
            return false;
        }

        mDisplayShader.reset(program);
    }

    // 创建顶点缓冲
    if (!mQuadVBO) {
        BufferDescriptor vboDesc;
        vboDesc.size = sizeof(kQuadVerticesExt);
        vboDesc.type = BufferType::Vertex;
        vboDesc.usage = BufferUsage::Static;
        vboDesc.data = kQuadVerticesExt;
        vboDesc.stride = 4 * sizeof(float);
        vboDesc.debugName = "OutputExtQuadVBO";

        mQuadVBO.reset(mRenderContext->CreateVertexBuffer(vboDesc));

        if (mQuadVBO) {
            VertexLayoutDescriptor layout;
            layout.stride = 4 * sizeof(float);
            layout.attributes = {
                {0, VertexFormat::Float2, 0, layout.stride, false},
                {1, VertexFormat::Float2, 2 * sizeof(float), layout.stride, false}
            };
            mQuadVBO->SetVertexLayout(layout);
        }
    }

    if (!mQuadVBO) {
        return false;
    }

    int32_t texWidth = texture->GetWidth();
    int32_t texHeight = texture->GetHeight();

    int32_t viewX = config.x;
    int32_t viewY = config.y;
    int32_t viewW = config.width > 0 ? config.width : texWidth;
    int32_t viewH = config.height > 0 ? config.height : texHeight;

    float texAspect = static_cast<float>(texWidth) / texHeight;
    float viewAspect = static_cast<float>(viewW) / viewH;

    int32_t renderX = viewX;
    int32_t renderY = viewY;
    int32_t renderW = viewW;
    int32_t renderH = viewH;

    switch (config.scaleMode) {
        case OutputEntity::ScaleMode::Fit:
            if (texAspect > viewAspect) {
                renderH = static_cast<int32_t>(viewW / texAspect);
                renderY = viewY + (viewH - renderH) / 2;
            } else {
                renderW = static_cast<int32_t>(viewH * texAspect);
                renderX = viewX + (viewW - renderW) / 2;
            }
            break;

        case OutputEntity::ScaleMode::Fill:
            if (texAspect > viewAspect) {
                renderW = static_cast<int32_t>(viewH * texAspect);
                renderX = viewX + (viewW - renderW) / 2;
            } else {
                renderH = static_cast<int32_t>(viewW / texAspect);
                renderY = viewY + (viewH - renderH) / 2;
            }
            break;

        case OutputEntity::ScaleMode::Stretch:
            break;
    }

    float scaleX = static_cast<float>(renderW) / viewW;
    float scaleY = static_cast<float>(renderH) / viewH;
    float offsetX = (static_cast<float>(renderX - viewX) / viewW) * 2.0f - 1.0f + scaleX;
    float offsetY = (static_cast<float>(renderY - viewY) / viewH) * 2.0f - 1.0f + scaleY;

    float mvpMatrix[16] = {
        scaleX,   0.0f,     0.0f,  0.0f,
        0.0f,     scaleY,   0.0f,  0.0f,
        0.0f,     0.0f,     1.0f,  0.0f,
        offsetX - scaleX, offsetY - scaleY, 0.0f, 1.0f
    };

    mRenderContext->BeginRenderPass(nullptr);
    mRenderContext->SetViewport(viewX, viewY, viewW, viewH);
    mRenderContext->ClearColor(config.backgroundColor[0], config.backgroundColor[1],
                               config.backgroundColor[2], config.backgroundColor[3]);

    mDisplayShader->Use();
    mDisplayShader->SetUniformMatrix4("uMVPMatrix", mvpMatrix, false);
    mDisplayShader->SetUniform("uTexture", 0);

    mRenderContext->SetTexture(texture.get(), 0);
    mRenderContext->SetVertexBuffer(mQuadVBO.get(), 0);
    mRenderContext->Draw(0, 4);

    mRenderContext->EndRenderPass();
    mRenderContext->Present();

    return true;
}

bool OutputEntityExt::outputToEncoder(const EncoderOutputConfig& config, FramePacketPtr input) {
    if (!mRenderContext) {
        return false;
    }

    auto texture = input ? input->getTexture() : nullptr;
    if (!texture) {
        return false;
    }

    using namespace lrengine::render;

    // 复用显示着色器
    if (!mDisplayShader) {
        ShaderDescriptor vsDesc;
        vsDesc.stage = ShaderStage::Vertex;
        vsDesc.language = ShaderLanguage::GLSL;
        vsDesc.source = kDisplayVertexShaderExt;
        vsDesc.debugName = "OutputExtEncoderVS";

        LRShader* vertexShader = mRenderContext->CreateShader(vsDesc);
        if (!vertexShader || !vertexShader->IsCompiled()) {
            return false;
        }

        ShaderDescriptor fsDesc;
        fsDesc.stage = ShaderStage::Fragment;
        fsDesc.language = ShaderLanguage::GLSL;
        fsDesc.source = kDisplayFragmentShaderExt;
        fsDesc.debugName = "OutputExtEncoderFS";

        LRShader* fragmentShader = mRenderContext->CreateShader(fsDesc);
        if (!fragmentShader || !fragmentShader->IsCompiled()) {
            delete vertexShader;
            return false;
        }

        LRShaderProgram* program = mRenderContext->CreateShaderProgram(vertexShader, fragmentShader);
        if (!program || !program->IsLinked()) {
            delete vertexShader;
            delete fragmentShader;
            return false;
        }

        mDisplayShader.reset(program);
    }

    if (!mQuadVBO) {
        BufferDescriptor vboDesc;
        vboDesc.size = sizeof(kQuadVerticesExt);
        vboDesc.type = BufferType::Vertex;
        vboDesc.usage = BufferUsage::Static;
        vboDesc.data = kQuadVerticesExt;
        vboDesc.stride = 4 * sizeof(float);
        vboDesc.debugName = "OutputExtEncoderQuadVBO";

        mQuadVBO.reset(mRenderContext->CreateVertexBuffer(vboDesc));

        if (mQuadVBO) {
            VertexLayoutDescriptor layout;
            layout.stride = 4 * sizeof(float);
            layout.attributes = {
                {0, VertexFormat::Float2, 0, layout.stride, false},
                {1, VertexFormat::Float2, 2 * sizeof(float), layout.stride, false}
            };
            mQuadVBO->SetVertexLayout(layout);
        }
    }

    if (!mQuadVBO) {
        return false;
    }

    int32_t texWidth = texture->GetWidth();
    int32_t texHeight = texture->GetHeight();

    // 单位矩阵（编码器通常不需要额外缩放）
    float mvpMatrix[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };

    mRenderContext->BeginRenderPass(nullptr);
    mRenderContext->SetViewport(0, 0, texWidth, texHeight);
    mRenderContext->ClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    mDisplayShader->Use();
    mDisplayShader->SetUniformMatrix4("uMVPMatrix", mvpMatrix, false);
    mDisplayShader->SetUniform("uTexture", 0);

    mRenderContext->SetTexture(texture.get(), 0);
    mRenderContext->SetVertexBuffer(mQuadVBO.get(), 0);
    mRenderContext->Draw(0, 4);

    mRenderContext->EndRenderPass();
    mRenderContext->Present();

    return true;
}

bool OutputEntityExt::executeCallback(const CallbackOutputConfig& config, FramePacketPtr input) {
    // TODO: 实现
    if (config.frameCallback) {
        config.frameCallback(input);
        return true;
    }
    return false;
}

bool OutputEntityExt::outputTexture(const TextureOutputConfig& config, FramePacketPtr input) {
    (void)config;
    // 纹理输出对当前实现来说是透明的：直接通过 FramePacket 的纹理给下游/调用方使用
    // 因此这里不需要额外处理，返回 true 即可。
    return input != nullptr && input->getTexture() != nullptr;
}

bool OutputEntityExt::saveToFile(const FileOutputConfig& config, FramePacketPtr input) {
    if (!input || config.filePath.empty()) {
        return false;
    }

    // 确保有CPU数据
    const uint8_t* cpuBuffer = input->getCpuBuffer();
    if (!cpuBuffer) {
        return false;
    }

    // 目前仅占位实现：实际的 PNG/JPEG 编码在后续阶段接入
    // 这里返回 true，表示已经消费该输出目标，避免影响其他目标。
    (void)config;
    return true;
}

bool OutputEntityExt::outputToPlatform(const PlatformOutputConfig& config, FramePacketPtr input) {
    (void)config;
    (void)input;

    // 平台特定输出（CVPixelBuffer/SurfaceTexture）依赖后续 PlatformContext 与 LREngine 的扩展
    // 当前阶段保留占位实现，返回 false 表示未处理，由上层决定是否忽略。
    return false;
}

bool OutputEntityExt::convertFormat(FramePacketPtr input, OutputDataFormat targetFormat, uint8_t* outputBuffer) {
    if (!input || !outputBuffer) {
        return false;
    }

    const uint8_t* src = input->getCpuBuffer();
    if (!src) {
        return false;
    }

    uint32_t width = input->getWidth();
    uint32_t height = input->getHeight();
    if (width == 0 || height == 0) {
        return false;
    }

    size_t pixelCount = static_cast<size_t>(width) * height;
    PixelFormat srcFormat = input->getFormat();

    switch (targetFormat) {
        case OutputDataFormat::RGBA8: {
            // 如果源就是RGBA8，直接拷贝
            if (srcFormat == PixelFormat::RGBA8) {
                std::memcpy(outputBuffer, src, pixelCount * 4);
                return true;
            }
            // 源为BGRA8，做简单通道交换
            if (srcFormat == PixelFormat::BGRA8) {
                for (size_t i = 0; i < pixelCount; ++i) {
                    const uint8_t* p = src + i * 4;
                    uint8_t* d = outputBuffer + i * 4;
                    d[0] = p[2]; // R
                    d[1] = p[1]; // G
                    d[2] = p[0]; // B
                    d[3] = p[3]; // A
                }
                return true;
            }
            return false;
        }
        case OutputDataFormat::BGRA8: {
            if (srcFormat == PixelFormat::BGRA8) {
                std::memcpy(outputBuffer, src, pixelCount * 4);
                return true;
            }
            if (srcFormat == PixelFormat::RGBA8) {
                for (size_t i = 0; i < pixelCount; ++i) {
                    const uint8_t* p = src + i * 4;
                    uint8_t* d = outputBuffer + i * 4;
                    d[0] = p[2]; // B
                    d[1] = p[1]; // G
                    d[2] = p[0]; // R
                    d[3] = p[3]; // A
                }
                return true;
            }
            return false;
        }
        case OutputDataFormat::NV12:
        case OutputDataFormat::NV21:
        case OutputDataFormat::YUV420P:
        case OutputDataFormat::Texture:
        default:
            // 这些格式转换在后续阶段实现
            return false;
    }
}

} // namespace pipeline
