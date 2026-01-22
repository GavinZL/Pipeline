/**
 * @file IOEntity.cpp
 * @brief 输入输出节点实现 - 管线的边界
 */

#include "pipeline/entity/IOEntity.h"
#include "pipeline/data/FramePacket.h"
#include "pipeline/data/FramePort.h"
#include "pipeline/pool/TexturePool.h"
#include "pipeline/pool/FramePacketPool.h"
#include "pipeline/core/PipelineConfig.h"

// LREngine headers
#include <lrengine/core/LRRenderContext.h>
#include <lrengine/core/LRShader.h>
#include <lrengine/core/LRTexture.h>
#include <lrengine/core/LRFrameBuffer.h>
#include <lrengine/core/LRBuffer.h>
#include <lrengine/core/LRPipelineState.h>
#include <lrengine/core/LRTypes.h>

#include <cstring>
#include <cmath>

#ifdef __ANDROID__
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#endif

namespace pipeline {

// =============================================================================
// 着色器源码
// =============================================================================

namespace {

// YUV转RGB着色器
const char* kYUVConversionVertexShader = R"(
attribute vec4 aPosition;
attribute vec2 aTexCoord;
varying vec2 vTexCoord;

void main() {
    gl_Position = aPosition;
    vTexCoord = aTexCoord;
}
)";

const char* kYUV420ConversionFragmentShader = R"(
precision mediump float;
varying vec2 vTexCoord;

uniform sampler2D uYTexture;
uniform sampler2D uUTexture;
uniform sampler2D uVTexture;

// BT.601标准转换矩阵
const mat3 yuv2rgb = mat3(
    1.0,     1.0,      1.0,
    0.0,    -0.344,    1.772,
    1.402,  -0.714,    0.0
);

void main() {
    float y = texture2D(uYTexture, vTexCoord).r;
    float u = texture2D(uUTexture, vTexCoord).r - 0.5;
    float v = texture2D(uVTexture, vTexCoord).r - 0.5;
    
    vec3 yuv = vec3(y, u, v);
    vec3 rgb = yuv2rgb * yuv;
    
    gl_FragColor = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}
)";

const char* kNV12ConversionFragmentShader = R"(
precision mediump float;
varying vec2 vTexCoord;

uniform sampler2D uYTexture;
uniform sampler2D uUVTexture;
uniform bool uIsNV21;

const mat3 yuv2rgb = mat3(
    1.0,     1.0,      1.0,
    0.0,    -0.344,    1.772,
    1.402,  -0.714,    0.0
);

void main() {
    float y = texture2D(uYTexture, vTexCoord).r;
    vec2 uv_value = texture2D(uUVTexture, vTexCoord).rg;
    
    float u, v;
    if (uIsNV21) {
        v = uv_value.r - 0.5;
        u = uv_value.g - 0.5;
    } else {
        u = uv_value.r - 0.5;
        v = uv_value.g - 0.5;
    }
    
    vec3 yuv = vec3(y, u, v);
    vec3 rgb = yuv2rgb * yuv;
    
    gl_FragColor = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}
)";

// OES纹理转换着色器（Android）
const char* kOESConversionVertexShader = R"(
attribute vec4 aPosition;
attribute vec2 aTexCoord;
varying vec2 vTexCoord;
uniform mat4 uTexMatrix;

void main() {
    gl_Position = aPosition;
    vTexCoord = (uTexMatrix * vec4(aTexCoord, 0.0, 1.0)).xy;
}
)";

const char* kOESConversionFragmentShader = R"(
#extension GL_OES_EGL_image_external : require
precision mediump float;
varying vec2 vTexCoord;
uniform samplerExternalOES uOESTexture;

void main() {
    gl_FragColor = texture2D(uOESTexture, vTexCoord);
}
)";

// 显示/输出着色器
const char* kDisplayVertexShader = R"(
attribute vec4 aPosition;
attribute vec2 aTexCoord;
varying vec2 vTexCoord;
uniform mat4 uMVPMatrix;

void main() {
    gl_Position = uMVPMatrix * aPosition;
    vTexCoord = aTexCoord;
}
)";

const char* kDisplayFragmentShader = R"(
precision mediump float;
varying vec2 vTexCoord;
uniform sampler2D uTexture;

void main() {
    gl_FragColor = texture2D(uTexture, vTexCoord);
}
)";

// 单位矩阵
const float kIdentityMatrix[16] = {
    1, 0, 0, 0,
    0, 1, 0, 0,
    0, 0, 1, 0,
    0, 0, 0, 1
};

// 全屏四边形顶点数据 (position.xy, texcoord.xy)
const float kQuadVertices[] = {
    // position     texcoord
    -1.0f, -1.0f,   0.0f, 0.0f,  // 左下
     1.0f, -1.0f,   1.0f, 0.0f,  // 右下
    -1.0f,  1.0f,   0.0f, 1.0f,  // 左上
     1.0f,  1.0f,   1.0f, 1.0f,  // 右上
};

} // anonymous namespace

// =============================================================================
// InputEntity 实现
// =============================================================================

InputEntity::InputEntity(const std::string& name)
    : ProcessEntity(name) {
    // 创建输出端口
    addOutputPort("output");
}

InputEntity::~InputEntity() = default;

void InputEntity::setRenderContext(lrengine::render::LRRenderContext* context) {
    mRenderContext = context;
}

void InputEntity::setRotation(int32_t degrees) {
    // 规范化为0, 90, 180, 270
    mRotation = ((degrees % 360) + 360) % 360;
    mRotation = (mRotation / 90) * 90;
}

// =============================================================================
// 数据输入接口
// =============================================================================

FramePacketPtr InputEntity::feedRGBA(const uint8_t* data, 
                                     uint32_t width, 
                                     uint32_t height,
                                     uint32_t stride,
                                     uint64_t timestamp) {
    if (!data || width == 0 || height == 0) {
        return nullptr;
    }
    
    if (stride == 0) {
        stride = width * 4;
    }
    
    // 创建FramePacket
    auto packet = std::make_shared<FramePacket>();
    packet->setSize(width, height);
    packet->setFormat(PixelFormat::RGBA8);
    packet->setTimestamp(timestamp);
    packet->setFrameId(mFrameCounter++);
    
    // TODO: 使用TexturePool获取纹理并上传数据
    // auto texture = mTexturePool->acquire(width, height, PixelFormat::RGBA8);
    // texture->upload(data, width, height, stride);
    // packet->setTexture(texture);
    
    // 临时：存储CPU数据
    size_t dataSize = height * stride;
    std::vector<uint8_t> cpuBuffer(dataSize);
    std::memcpy(cpuBuffer.data(), data, dataSize);
    packet->setCpuBuffer(std::move(cpuBuffer).data(), dataSize, true);
    
    // 存储为待处理数据包
    {
        std::lock_guard<std::mutex> lock(mPendingMutex);
        mPendingPacket = packet;
    }
    
    return packet;
}

FramePacketPtr InputEntity::feedYUV420(const uint8_t* yData,
                                       const uint8_t* uData,
                                       const uint8_t* vData,
                                       uint32_t width,
                                       uint32_t height,
                                       uint32_t yStride,
                                       uint32_t uvStride,
                                       uint64_t timestamp) {
    if (!yData || !uData || !vData || width == 0 || height == 0) {
        return nullptr;
    }
    
    if (yStride == 0) yStride = width;
    if (uvStride == 0) uvStride = width / 2;
    
    // 创建FramePacket
    auto packet = std::make_shared<FramePacket>();
    packet->setSize(width, height);
    packet->setFormat(PixelFormat::RGBA8);  // 输出为RGBA
    packet->setTimestamp(timestamp);
    packet->setFrameId(mFrameCounter++);
    
    // 执行YUV到RGBA转换
    auto texture = convertYUVToRGBA(yData, uData, vData, width, height, yStride, uvStride);
    if (texture) {
        packet->setTexture(texture);
    }
    
    // 存储为待处理数据包
    {
        std::lock_guard<std::mutex> lock(mPendingMutex);
        mPendingPacket = packet;
    }
    
    return packet;
}

FramePacketPtr InputEntity::feedNV12(const uint8_t* yData,
                                     const uint8_t* uvData,
                                     uint32_t width,
                                     uint32_t height,
                                     bool isNV21,
                                     uint64_t timestamp) {
    if (!yData || !uvData || width == 0 || height == 0) {
        return nullptr;
    }
    
    // 创建FramePacket
    auto packet = std::make_shared<FramePacket>();
    packet->setSize(width, height);
    packet->setFormat(PixelFormat::RGBA8);
    packet->setTimestamp(timestamp);
    packet->setFrameId(mFrameCounter++);
    packet->setMetadata("isNV21", isNV21);
    
    // TODO: 实现NV12到RGBA的GPU转换
    // 需要创建Y纹理和UV纹理，然后用着色器转换
    
    // 存储为待处理数据包
    {
        std::lock_guard<std::mutex> lock(mPendingMutex);
        mPendingPacket = packet;
    }
    
    return packet;
}

FramePacketPtr InputEntity::feedTexture(std::shared_ptr<lrengine::render::LRTexture> texture,
                                        uint32_t width,
                                        uint32_t height,
                                        uint64_t timestamp) {
    if (!texture || width == 0 || height == 0) {
        return nullptr;
    }
    
    // 创建FramePacket
    auto packet = std::make_shared<FramePacket>();
    packet->setSize(width, height);
    packet->setFormat(PixelFormat::RGBA8);
    packet->setTimestamp(timestamp);
    packet->setFrameId(mFrameCounter++);
    packet->setTexture(texture);
    
    // 存储为待处理数据包
    {
        std::lock_guard<std::mutex> lock(mPendingMutex);
        mPendingPacket = packet;
    }
    
    return packet;
}

FramePacketPtr InputEntity::feedOES(uint32_t oesTextureId,
                                    uint32_t width,
                                    uint32_t height,
                                    const float* transformMatrix,
                                    uint64_t timestamp) {
    if (oesTextureId == 0 || width == 0 || height == 0) {
        return nullptr;
    }
    
    // 创建FramePacket
    auto packet = std::make_shared<FramePacket>();
    packet->setSize(width, height);
    packet->setFormat(PixelFormat::RGBA8);
    packet->setTimestamp(timestamp);
    packet->setFrameId(mFrameCounter++);
    
    // 执行OES到RGBA的转换
    auto texture = convertOESToRGBA(oesTextureId, width, height, transformMatrix);
    if (texture) {
        packet->setTexture(texture);
    }
    
    // 存储为待处理数据包
    {
        std::lock_guard<std::mutex> lock(mPendingMutex);
        mPendingPacket = packet;
    }
    
    return packet;
}

// =============================================================================
// 处理实现
// =============================================================================

bool InputEntity::process(const std::vector<FramePacketPtr>& inputs,
                          std::vector<FramePacketPtr>& outputs,
                          PipelineContext& context) {
    // InputEntity不使用输入，直接输出pending的数据包
    FramePacketPtr packet;
    {
        std::lock_guard<std::mutex> lock(mPendingMutex);
        packet = mPendingPacket;
        mPendingPacket = nullptr;
    }
    
    if (!packet) {
        return false;
    }
    
    // 应用旋转和翻转
    if (mRotation != 0 || mFlipHorizontal || mFlipVertical) {
        // TODO: 应用变换
        // 可以通过设置纹理矩阵或在下一个GPU节点中处理
        packet->setMetadata("rotation", mRotation);
        packet->setMetadata("flipH", mFlipHorizontal);
        packet->setMetadata("flipV", mFlipVertical);
    }
    
    outputs.push_back(packet);
    return true;
}

bool InputEntity::createYUVConversionShader() {
    if (!mRenderContext) return false;
    if (mYUVShader) return true;
    
    using namespace lrengine::render;
    
    // 创建顶点着色器
    ShaderDescriptor vsDesc;
    vsDesc.stage = ShaderStage::Vertex;
    vsDesc.language = ShaderLanguage::GLSL;
    vsDesc.source = kYUVConversionVertexShader;
    vsDesc.debugName = "YUVConversionVS";
    
    LRShader* vertexShader = mRenderContext->CreateShader(vsDesc);
    if (!vertexShader || !vertexShader->IsCompiled()) {
        return false;
    }
    
    // 创建片段着色器
    ShaderDescriptor fsDesc;
    fsDesc.stage = ShaderStage::Fragment;
    fsDesc.language = ShaderLanguage::GLSL;
    fsDesc.source = kYUV420ConversionFragmentShader;
    fsDesc.debugName = "YUVConversionFS";
    
    LRShader* fragmentShader = mRenderContext->CreateShader(fsDesc);
    if (!fragmentShader || !fragmentShader->IsCompiled()) {
        delete vertexShader;
        return false;
    }
    
    // 创建着色器程序
    LRShaderProgram* program = mRenderContext->CreateShaderProgram(vertexShader, fragmentShader);
    if (!program || !program->IsLinked()) {
        delete vertexShader;
        delete fragmentShader;
        return false;
    }
    
    mYUVShader.reset(program);
    return true;
}

bool InputEntity::createOESConversionShader() {
    if (!mRenderContext) return false;
    if (mOESShader) return true;
    
    using namespace lrengine::render;
    
    // 创建顶点着色器
    ShaderDescriptor vsDesc;
    vsDesc.stage = ShaderStage::Vertex;
    vsDesc.language = ShaderLanguage::GLSL;
    vsDesc.source = kOESConversionVertexShader;
    vsDesc.debugName = "OESConversionVS";
    
    LRShader* vertexShader = mRenderContext->CreateShader(vsDesc);
    if (!vertexShader || !vertexShader->IsCompiled()) {
        return false;
    }
    
    // 创建片段着色器
    ShaderDescriptor fsDesc;
    fsDesc.stage = ShaderStage::Fragment;
    fsDesc.language = ShaderLanguage::GLSL;
    fsDesc.source = kOESConversionFragmentShader;
    fsDesc.debugName = "OESConversionFS";
    
    LRShader* fragmentShader = mRenderContext->CreateShader(fsDesc);
    if (!fragmentShader || !fragmentShader->IsCompiled()) {
        delete vertexShader;
        return false;
    }
    
    // 创建着色器程序
    LRShaderProgram* program = mRenderContext->CreateShaderProgram(vertexShader, fragmentShader);
    if (!program || !program->IsLinked()) {
        delete vertexShader;
        delete fragmentShader;
        return false;
    }
    
    mOESShader.reset(program);
    return true;
}

std::shared_ptr<lrengine::render::LRTexture> InputEntity::convertYUVToRGBA(
    const uint8_t* yData, const uint8_t* uData, const uint8_t* vData,
    uint32_t width, uint32_t height,
    uint32_t yStride, uint32_t uvStride) {
    
    if (!mRenderContext) return nullptr;
    
    // 确保着色器已创建
    if (!mYUVShader && !createYUVConversionShader()) {
        return nullptr;
    }
    
    using namespace lrengine::render;
    
    // 1. 创建/更新Y纹理
    if (!mYTexture || mYTexture->GetWidth() != width || mYTexture->GetHeight() != height) {
        TextureDescriptor yTexDesc;
        yTexDesc.width = width;
        yTexDesc.height = height;
        yTexDesc.format = lrengine::render::PixelFormat::R8;
        yTexDesc.type = TextureType::Texture2D;
        yTexDesc.sampler.minFilter = FilterMode::Linear;
        yTexDesc.sampler.magFilter = FilterMode::Linear;
        yTexDesc.sampler.wrapU = WrapMode::ClampToEdge;
        yTexDesc.sampler.wrapV = WrapMode::ClampToEdge;
        yTexDesc.debugName = "YTexture";
        
        mYTexture.reset(mRenderContext->CreateTexture(yTexDesc));
    }
    
    // 创建/更新U纹理
    uint32_t uvWidth = width / 2;
    uint32_t uvHeight = height / 2;
    if (!mUTexture || mUTexture->GetWidth() != uvWidth || mUTexture->GetHeight() != uvHeight) {
        TextureDescriptor uvTexDesc;
        uvTexDesc.width = uvWidth;
        uvTexDesc.height = uvHeight;
        uvTexDesc.format = lrengine::render::PixelFormat::R8;
        uvTexDesc.type = TextureType::Texture2D;
        uvTexDesc.sampler.minFilter = FilterMode::Linear;
        uvTexDesc.sampler.magFilter = FilterMode::Linear;
        uvTexDesc.sampler.wrapU = WrapMode::ClampToEdge;
        uvTexDesc.sampler.wrapV = WrapMode::ClampToEdge;
        
        uvTexDesc.debugName = "UTexture";
        mUTexture.reset(mRenderContext->CreateTexture(uvTexDesc));
        
        uvTexDesc.debugName = "VTexture";
        mVTexture.reset(mRenderContext->CreateTexture(uvTexDesc));
    }
    
    if (!mYTexture || !mUTexture || !mVTexture) {
        return nullptr;
    }
    
    // 上传YUV数据
    mYTexture->UpdateData(yData, nullptr);
    mUTexture->UpdateData(uData, nullptr);
    mVTexture->UpdateData(vData, nullptr);
    
    // 2. 创建输出RGBA纹理
    TextureDescriptor outTexDesc;
    outTexDesc.width = width;
    outTexDesc.height = height;
    outTexDesc.format = lrengine::render::PixelFormat::RGBA8;
    outTexDesc.type = TextureType::Texture2D;
    outTexDesc.sampler.minFilter = FilterMode::Linear;
    outTexDesc.sampler.magFilter = FilterMode::Linear;
    outTexDesc.sampler.wrapU = WrapMode::ClampToEdge;
    outTexDesc.sampler.wrapV = WrapMode::ClampToEdge;
    outTexDesc.debugName = "YUVConversionOutput";
    
    LRTexture* outputTexture = mRenderContext->CreateTexture(outTexDesc);
    if (!outputTexture) {
        return nullptr;
    }
    
    // 3. 创建或复用FBO
    if (!mConversionFBO) {
        FrameBufferDescriptor fboDesc;
        fboDesc.width = width;
        fboDesc.height = height;
        fboDesc.hasDepthStencil = false;
        fboDesc.debugName = "YUVConversionFBO";
        
        mConversionFBO.reset(mRenderContext->CreateFrameBuffer(fboDesc));
    }
    
    if (!mConversionFBO) {
        delete outputTexture;
        return nullptr;
    }
    
    mConversionFBO->AttachColorTexture(outputTexture, 0);
    
    if (!mConversionFBO->IsComplete()) {
        delete outputTexture;
        return nullptr;
    }
    
    // 4. 创建顶点缓冲区（如果尚未创建）
    if (!mQuadVBO) {
        BufferDescriptor vboDesc;
        vboDesc.size = sizeof(kQuadVertices);
        vboDesc.type = BufferType::Vertex;
        vboDesc.usage = BufferUsage::Static;
        vboDesc.data = kQuadVertices;
        vboDesc.stride = 4 * sizeof(float);
        vboDesc.debugName = "QuadVBO";
        
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
        delete outputTexture;
        return nullptr;
    }
    
    // 5. 执行YUV到RGBA转换渲染
    mRenderContext->BeginRenderPass(mConversionFBO.get());
    mRenderContext->SetViewport(0, 0, width, height);
    mRenderContext->ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    
    mYUVShader->Use();
    mYUVShader->SetUniform("uYTexture", 0);
    mYUVShader->SetUniform("uUTexture", 1);
    mYUVShader->SetUniform("uVTexture", 2);
    
    mRenderContext->SetTexture(mYTexture.get(), 0);
    mRenderContext->SetTexture(mUTexture.get(), 1);
    mRenderContext->SetTexture(mVTexture.get(), 2);
    
    mRenderContext->SetVertexBuffer(mQuadVBO.get(), 0);
    mRenderContext->Draw(0, 4);
    
    mRenderContext->EndRenderPass();
    
    return std::shared_ptr<LRTexture>(outputTexture);
}

std::shared_ptr<lrengine::render::LRTexture> InputEntity::convertOESToRGBA(
    uint32_t oesTextureId,
    uint32_t width, uint32_t height,
    const float* transformMatrix) {
    
    if (!mRenderContext) return nullptr;
    
    // 确保着色器已创建
    if (!mOESShader && !createOESConversionShader()) {
        return nullptr;
    }
    
    // 使用单位矩阵如果没有提供变换矩阵
    const float* matrix = transformMatrix ? transformMatrix : kIdentityMatrix;
    
    using namespace lrengine::render;
    
    // 1. 创建输出RGBA纹理
    TextureDescriptor texDesc;
    texDesc.width = width;
    texDesc.height = height;
    texDesc.format = lrengine::render::PixelFormat::RGBA8;
    texDesc.type = TextureType::Texture2D;
    texDesc.sampler.minFilter = FilterMode::Linear;
    texDesc.sampler.magFilter = FilterMode::Linear;
    texDesc.sampler.wrapU = WrapMode::ClampToEdge;
    texDesc.sampler.wrapV = WrapMode::ClampToEdge;
    texDesc.debugName = "OESConversionOutput";
    
    LRTexture* outputTexture = mRenderContext->CreateTexture(texDesc);
    if (!outputTexture) {
        return nullptr;
    }
    
    // 2. 创建或复用FBO
    if (!mConversionFBO) {
        FrameBufferDescriptor fboDesc;
        fboDesc.width = width;
        fboDesc.height = height;
        fboDesc.hasDepthStencil = false;
        fboDesc.debugName = "OESConversionFBO";
        
        mConversionFBO.reset(mRenderContext->CreateFrameBuffer(fboDesc));
    }
    
    if (!mConversionFBO) {
        delete outputTexture;
        return nullptr;
    }
    
    // 附加输出纹理到FBO
    mConversionFBO->AttachColorTexture(outputTexture, 0);
    
    if (!mConversionFBO->IsComplete()) {
        delete outputTexture;
        return nullptr;
    }
    
    // 3. 创建顶点缓冲区（如果尚未创建）
    if (!mQuadVBO) {
        BufferDescriptor vboDesc;
        vboDesc.size = sizeof(kQuadVertices);
        vboDesc.type = BufferType::Vertex;
        vboDesc.usage = BufferUsage::Static;
        vboDesc.data = kQuadVertices;
        vboDesc.stride = 4 * sizeof(float);  // position.xy + texcoord.xy
        vboDesc.debugName = "QuadVBO";
        
        mQuadVBO.reset(mRenderContext->CreateVertexBuffer(vboDesc));
        
        if (mQuadVBO) {
            VertexLayoutDescriptor layout;
            layout.stride = 4 * sizeof(float);
            layout.attributes = {
                {0, VertexFormat::Float2, 0, layout.stride, false},                    // aPosition
                {1, VertexFormat::Float2, 2 * sizeof(float), layout.stride, false}     // aTexCoord
            };
            mQuadVBO->SetVertexLayout(layout);
        }
    }
    
    if (!mQuadVBO) {
        delete outputTexture;
        return nullptr;
    }
    
    // 4. 执行渲染
    mRenderContext->BeginRenderPass(mConversionFBO.get());
    mRenderContext->SetViewport(0, 0, width, height);
    mRenderContext->ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    
    // 使用着色器程序
    mOESShader->Use();
    mOESShader->SetUniformMatrix4("uTexMatrix", matrix, false);
    mOESShader->SetUniform("uOESTexture", 0);
    
    // 绑定OES纹理 (需要使用原生GL调用，因为OES是Android特有的)
#ifdef __ANDROID__
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, oesTextureId);
#endif
    
    // 设置顶点缓冲区并绘制
    mRenderContext->SetVertexBuffer(mQuadVBO.get(), 0);
    mRenderContext->Draw(0, 4);  // Triangle strip, 4 vertices
    
    mRenderContext->EndRenderPass();
    
    // 5. 返回包装的纹理
    return std::shared_ptr<LRTexture>(outputTexture);
}

// =============================================================================
// OutputEntity 实现
// =============================================================================

OutputEntity::OutputEntity(const std::string& name)
    : ProcessEntity(name) {
    // 创建输入端口
    addInputPort("input");
}

OutputEntity::~OutputEntity() = default;

void OutputEntity::setRenderContext(lrengine::render::LRRenderContext* context) {
    mRenderContext = context;
}

void OutputEntity::setDisplaySurface(void* surface) {
    mDisplaySurface = surface;
}

void OutputEntity::setEncoderSurface(void* surface) {
    mEncoderSurface = surface;
}

void OutputEntity::setFrameCallback(FrameCallback callback) {
    mFrameCallback = std::move(callback);
}

void OutputEntity::setOutputFilePath(const std::string& path) {
    mOutputFilePath = path;
}

void OutputEntity::setViewport(int32_t x, int32_t y, int32_t width, int32_t height) {
    mViewportX = x;
    mViewportY = y;
    mViewportWidth = width;
    mViewportHeight = height;
}

void OutputEntity::setBackgroundColor(float r, float g, float b, float a) {
    mBackgroundColor[0] = r;
    mBackgroundColor[1] = g;
    mBackgroundColor[2] = b;
    mBackgroundColor[3] = a;
}

FramePacketPtr OutputEntity::getLastOutput() const {
    std::lock_guard<std::mutex> lock(mLastOutputMutex);
    return mLastOutput;
}

std::shared_ptr<lrengine::render::LRTexture> OutputEntity::getOutputTexture() const {
    std::lock_guard<std::mutex> lock(mLastOutputMutex);
    if (mLastOutput) {
        return mLastOutput->getTexture();
    }
    return nullptr;
}

size_t OutputEntity::readPixels(uint8_t* buffer, size_t bufferSize) {
    std::lock_guard<std::mutex> lock(mLastOutputMutex);
    if (!mLastOutput) {
        return 0;
    }
    
    // 尝试从CPU缓冲区获取
    const auto& cpuBuffer = mLastOutput->getCpuBuffer();
    if (!cpuBuffer) {
        std::memcpy(buffer, cpuBuffer, bufferSize);
        return bufferSize;
    }
    
    // TODO: 从GPU纹理读取
    // auto texture = mLastOutput->getTexture();
    // if (texture) {
    //     texture->readPixels(buffer, bufferSize);
    //     return width * height * 4;
    // }
    
    return 0;
}

// =============================================================================
// 处理实现
// =============================================================================

bool OutputEntity::process(const std::vector<FramePacketPtr>& inputs,
                          std::vector<FramePacketPtr>& outputs,
                          PipelineContext& context) {
    if (inputs.empty() || !inputs[0]) {
        return false;
    }
    
    FramePacketPtr input = inputs[0];
    
    // 保存最后输出
    {
        std::lock_guard<std::mutex> lock(mLastOutputMutex);
        mLastOutput = input;
    }
    
    // 根据输出目标执行相应操作
    bool success = true;
    switch (mOutputTarget) {
        case OutputTarget::Display:
            success = renderToDisplay(input);
            break;
            
        case OutputTarget::Encoder:
            success = renderToEncoder(input);
            break;
            
        case OutputTarget::Callback:
            executeCallback(input);
            break;
            
        case OutputTarget::Texture:
            // 纹理输出不需要额外处理，已经在mLastOutput中
            break;
            
        case OutputTarget::File:
            success = saveToFile(input);
            break;
    }
    
    // OutputEntity通常不产生输出给下游
    return success;
}

bool OutputEntity::renderToDisplay(FramePacketPtr input) {
    if (!mRenderContext || !mDisplaySurface) {
        return false;
    }
    
    auto texture = input->getTexture();
    if (!texture) {
        return false;
    }
    
    using namespace lrengine::render;
    
    // 确保显示着色器已创建
    if (!mDisplayShader) {
        // 创建顶点着色器
        ShaderDescriptor vsDesc;
        vsDesc.stage = ShaderStage::Vertex;
        vsDesc.language = ShaderLanguage::GLSL;
        vsDesc.source = kDisplayVertexShader;
        vsDesc.debugName = "DisplayVS";
        
        LRShader* vertexShader = mRenderContext->CreateShader(vsDesc);
        if (!vertexShader || !vertexShader->IsCompiled()) {
            return false;
        }
        
        // 创建片段着色器
        ShaderDescriptor fsDesc;
        fsDesc.stage = ShaderStage::Fragment;
        fsDesc.language = ShaderLanguage::GLSL;
        fsDesc.source = kDisplayFragmentShader;
        fsDesc.debugName = "DisplayFS";
        
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
    
    // 创建顶点缓冲区（如果尚未创建）
    if (!mDisplayQuadVBO) {
        BufferDescriptor vboDesc;
        vboDesc.size = sizeof(kQuadVertices);
        vboDesc.type = BufferType::Vertex;
        vboDesc.usage = BufferUsage::Static;
        vboDesc.data = kQuadVertices;
        vboDesc.stride = 4 * sizeof(float);
        vboDesc.debugName = "DisplayQuadVBO";
        
        mDisplayQuadVBO.reset(mRenderContext->CreateVertexBuffer(vboDesc));
        
        if (mDisplayQuadVBO) {
            VertexLayoutDescriptor layout;
            layout.stride = 4 * sizeof(float);
            layout.attributes = {
                {0, VertexFormat::Float2, 0, layout.stride, false},                    // aPosition
                {1, VertexFormat::Float2, 2 * sizeof(float), layout.stride, false}     // aTexCoord
            };
            mDisplayQuadVBO->SetVertexLayout(layout);
        }
    }
    
    if (!mDisplayQuadVBO) {
        return false;
    }
    
    // 计算视口和变换矩阵
    int32_t texWidth = texture->GetWidth();
    int32_t texHeight = texture->GetHeight();
    
    int32_t viewX = mViewportX;
    int32_t viewY = mViewportY;
    int32_t viewW = mViewportWidth > 0 ? mViewportWidth : texWidth;
    int32_t viewH = mViewportHeight > 0 ? mViewportHeight : texHeight;
    
    // 根据缩放模式计算实际渲染区域
    float texAspect = static_cast<float>(texWidth) / texHeight;
    float viewAspect = static_cast<float>(viewW) / viewH;
    
    int32_t renderX = viewX;
    int32_t renderY = viewY;
    int32_t renderW = viewW;
    int32_t renderH = viewH;
    
    switch (mScaleMode) {
        case ScaleMode::Fit:
            if (texAspect > viewAspect) {
                renderH = static_cast<int32_t>(viewW / texAspect);
                renderY = viewY + (viewH - renderH) / 2;
            } else {
                renderW = static_cast<int32_t>(viewH * texAspect);
                renderX = viewX + (viewW - renderW) / 2;
            }
            break;
            
        case ScaleMode::Fill:
            if (texAspect > viewAspect) {
                renderW = static_cast<int32_t>(viewH * texAspect);
                renderX = viewX + (viewW - renderW) / 2;
            } else {
                renderH = static_cast<int32_t>(viewW / texAspect);
                renderY = viewY + (viewH - renderH) / 2;
            }
            break;
            
        case ScaleMode::Stretch:
            break;
    }
    
    // 计算MVP矩阵 (正交投影 + 缩放/平移)
    // 将渲染区域映射到NDC空间 [-1, 1]
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
    
    // 渲染到默认帧缓冲（Display Surface）
    mRenderContext->BeginRenderPass(nullptr);  // nullptr = 默认帧缓冲
    
    // 设置整个视口并清除背景
    mRenderContext->SetViewport(viewX, viewY, viewW, viewH);
    mRenderContext->ClearColor(mBackgroundColor[0], mBackgroundColor[1], 
                               mBackgroundColor[2], mBackgroundColor[3]);
    
    // 使用着色器
    mDisplayShader->Use();
    mDisplayShader->SetUniformMatrix4("uMVPMatrix", mvpMatrix, false);
    mDisplayShader->SetUniform("uTexture", 0);
    
    // 绑定纹理
    mRenderContext->SetTexture(texture.get(), 0);
    
    // 绘制
    mRenderContext->SetVertexBuffer(mDisplayQuadVBO.get(), 0);
    mRenderContext->Draw(0, 4);  // Triangle strip
    
    mRenderContext->EndRenderPass();
    
    // 呈现（交换缓冲区）
    mRenderContext->Present();
    
    return true;
}

bool OutputEntity::renderToEncoder(FramePacketPtr input) {
    if (!mRenderContext || !mEncoderSurface) {
        return false;
    }
    
    auto texture = input->getTexture();
    if (!texture) {
        return false;
    }
    
    using namespace lrengine::render;
    
    // 确保显示着色器已创建（复用Display着色器）
    if (!mDisplayShader) {
        ShaderDescriptor vsDesc;
        vsDesc.stage = ShaderStage::Vertex;
        vsDesc.language = ShaderLanguage::GLSL;
        vsDesc.source = kDisplayVertexShader;
        vsDesc.debugName = "EncoderVS";
        
        LRShader* vertexShader = mRenderContext->CreateShader(vsDesc);
        if (!vertexShader || !vertexShader->IsCompiled()) {
            return false;
        }
        
        ShaderDescriptor fsDesc;
        fsDesc.stage = ShaderStage::Fragment;
        fsDesc.language = ShaderLanguage::GLSL;
        fsDesc.source = kDisplayFragmentShader;
        fsDesc.debugName = "EncoderFS";
        
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
    
    // 创建顶点缓冲区（如果尚未创建）
    if (!mDisplayQuadVBO) {
        BufferDescriptor vboDesc;
        vboDesc.size = sizeof(kQuadVertices);
        vboDesc.type = BufferType::Vertex;
        vboDesc.usage = BufferUsage::Static;
        vboDesc.data = kQuadVertices;
        vboDesc.stride = 4 * sizeof(float);
        vboDesc.debugName = "EncoderQuadVBO";
        
        mDisplayQuadVBO.reset(mRenderContext->CreateVertexBuffer(vboDesc));
        
        if (mDisplayQuadVBO) {
            VertexLayoutDescriptor layout;
            layout.stride = 4 * sizeof(float);
            layout.attributes = {
                {0, VertexFormat::Float2, 0, layout.stride, false},
                {1, VertexFormat::Float2, 2 * sizeof(float), layout.stride, false}
            };
            mDisplayQuadVBO->SetVertexLayout(layout);
        }
    }
    
    if (!mDisplayQuadVBO) {
        return false;
    }
    
    int32_t texWidth = texture->GetWidth();
    int32_t texHeight = texture->GetHeight();
    
    // 单位矩阵（编码器通常不需要缩放）
    float mvpMatrix[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    
    // 渲染到编码器FBO（如果存在）或默认帧缓冲
    mRenderContext->BeginRenderPass(mEncoderFBO.get());
    mRenderContext->SetViewport(0, 0, texWidth, texHeight);
    mRenderContext->ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    
    mDisplayShader->Use();
    mDisplayShader->SetUniformMatrix4("uMVPMatrix", mvpMatrix, false);
    mDisplayShader->SetUniform("uTexture", 0);
    
    mRenderContext->SetTexture(texture.get(), 0);
    mRenderContext->SetVertexBuffer(mDisplayQuadVBO.get(), 0);
    mRenderContext->Draw(0, 4);
    
    mRenderContext->EndRenderPass();
    
    // 设置呈现时间戳并交换缓冲区
    // 注意：setPresentationTime 需要平台特定的EGL扩展调用
    // 在Android上：eglPresentationTimeANDROID(display, surface, timestamp)
#ifdef __ANDROID__
    // 时间戳由JNI层通过EGL扩展设置
#endif
    
    mRenderContext->Present();
    
    return true;
}

void OutputEntity::executeCallback(FramePacketPtr input) {
    if (mFrameCallback) {
        mFrameCallback(input);
    }
}

bool OutputEntity::saveToFile(FramePacketPtr input) {
    if (mOutputFilePath.empty()) {
        return false;
    }
    
    // 确保有CPU数据
    const auto& cpuBuffer = input->getCpuBuffer();
    if (!cpuBuffer) {
        // 尝试从GPU读取
        // input->loadCpuBufferFromTexture();
    }
    
    // TODO: 保存为图片文件
    // 可以使用stb_image_write或平台特定API
    // stbi_write_png(mOutputFilePath.c_str(), 
    //     input->getWidth(), input->getHeight(), 4, 
    //     cpuBuffer.data(), input->getWidth() * 4);
    
    return true;
}

} // namespace pipeline
