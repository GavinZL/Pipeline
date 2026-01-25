/**
 * @file iOSMetalSurface.mm
 * @brief iOSMetalSurface 实现
 */

#if defined(__APPLE__)

#import "pipeline/output/ios/iOSMetalSurface.h"
#import "pipeline/utils/PipelineLog.h"
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <simd/simd.h>

namespace pipeline {
namespace output {
namespace ios {

// Metal Shader 源码
static NSString* const DISPLAY_SHADER_SOURCE = @R"(
#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float2 position [[attribute(0)]];
    float2 texCoord [[attribute(1)]];
};

struct VertexOut {
    float4 position [[position]];
    float2 texCoord;
};

vertex VertexOut displayVertex(VertexIn in [[stage_in]],
                                constant float4x4& transform [[buffer(1)]]) {
    VertexOut out;
    out.position = transform * float4(in.position, 0.0, 1.0);
    out.texCoord = in.texCoord;
    return out;
}

fragment float4 displayFragment(VertexOut in [[stage_in]],
                                 texture2d<float> tex [[texture(0)]],
                                 sampler samp [[sampler(0)]]) {
    return tex.sample(samp, in.texCoord);
}
)";

// 顶点数据
struct Vertex {
    simd_float2 position;
    simd_float2 texCoord;
};

static const Vertex SCREEN_VERTICES[] = {
    {{-1.0f, -1.0f}, {0.0f, 1.0f}},
    {{ 1.0f, -1.0f}, {1.0f, 1.0f}},
    {{-1.0f,  1.0f}, {0.0f, 0.0f}},
    {{ 1.0f,  1.0f}, {1.0f, 0.0f}},
};

// =============================================================================
// 构造与析构
// =============================================================================

iOSMetalSurface::iOSMetalSurface() {
    mPixelFormat = MTLPixelFormatBGRA8Unorm;
}

iOSMetalSurface::~iOSMetalSurface() {
    release();
}

// =============================================================================
// DisplaySurface 接口实现
// =============================================================================

bool iOSMetalSurface::initialize(lrengine::render::LRRenderContext* context) {
    mRenderContext = context;
    
    if (!mMetalManager) {
        PIPELINE_LOGE("MetalContextManager not set");
        return false;
    }
    
    // 获取 Metal 设备
    mDevice = mMetalManager->getMetalDevice();
    if (!mDevice) {
        PIPELINE_LOGE("Failed to get Metal device");
        return false;
    }
    
    // 创建命令队列
    id<MTLDevice> device = (__bridge id<MTLDevice>)mDevice;
    id<MTLCommandQueue> queue = [device newCommandQueue];
    mCommandQueue = (__bridge_retained void*)queue;
    
    // 配置 layer（如果已绑定）
    if (mMetalLayer) {
        if (!configureMetalLayer()) {
            return false;
        }
    }
    
    // 创建渲染管线
    if (!createRenderPipeline()) {
        return false;
    }
    
    mState = SurfaceState::Ready;
    PIPELINE_LOGI("iOSMetalSurface initialized");
    return true;
}

void iOSMetalSurface::release() {
    cleanupResources();
    
    if (mCommandQueue) {
        CFRelease(mCommandQueue);
        mCommandQueue = nullptr;
    }
    
    mMetalLayer = nullptr;
    mDevice = nullptr;
    mState = SurfaceState::Uninitialized;
    
    PIPELINE_LOGI("iOSMetalSurface released");
}

bool iOSMetalSurface::attachToLayer(void* layer) {
    if (!layer) {
        return false;
    }
    
    mMetalLayer = layer;
    
    CAMetalLayer* metalLayer = (__bridge CAMetalLayer*)layer;
    
    // 获取尺寸
    CGSize size = metalLayer.drawableSize;
    mWidth = static_cast<uint32_t>(size.width);
    mHeight = static_cast<uint32_t>(size.height);
    mScaleFactor = metalLayer.contentsScale;
    
    // 如果已初始化，配置 layer
    if (mDevice) {
        if (!configureMetalLayer()) {
            return false;
        }
    }
    
    PIPELINE_LOGI("Attached to Metal layer: %dx%d @%.1fx", mWidth, mHeight, mScaleFactor);
    return true;
}

void iOSMetalSurface::detach() {
    mMetalLayer = nullptr;
    mWidth = 0;
    mHeight = 0;
}

SurfaceSize iOSMetalSurface::getSize() const {
    return SurfaceSize{mWidth, mHeight, mScaleFactor};
}

void iOSMetalSurface::setSize(uint32_t width, uint32_t height) {
    if (mWidth != width || mHeight != height) {
        onSizeChanged(width, height);
    }
}

void iOSMetalSurface::onSizeChanged(uint32_t width, uint32_t height) {
    mWidth = width;
    mHeight = height;
    
    if (mMetalLayer) {
        CAMetalLayer* layer = (__bridge CAMetalLayer*)mMetalLayer;
        layer.drawableSize = CGSizeMake(width, height);
    }
    
    PIPELINE_LOGI("Surface size changed: %dx%d", width, height);
}

bool iOSMetalSurface::beginFrame() {
    if (mState != SurfaceState::Ready) {
        return false;
    }
    
    if (!mMetalLayer) {
        return false;
    }
    
    // 获取下一个 drawable
    if (!acquireNextDrawable()) {
        return false;
    }
    
    // 创建 command buffer
    id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)mCommandQueue;
    id<MTLCommandBuffer> cmdBuffer = [queue commandBuffer];
    mCurrentCommandBuffer = (__bridge_retained void*)cmdBuffer;
    
    mState = SurfaceState::Rendering;
    return true;
}

bool iOSMetalSurface::renderTexture(std::shared_ptr<lrengine::render::LRTexture> texture,
                                     const DisplayConfig& config) {
    if (mState != SurfaceState::Rendering) {
        return false;
    }
    
    if (!texture || !mCurrentDrawable || !mCurrentCommandBuffer) {
        return false;
    }
    
    id<CAMetalDrawable> drawable = (__bridge id<CAMetalDrawable>)mCurrentDrawable;
    id<MTLCommandBuffer> cmdBuffer = (__bridge id<MTLCommandBuffer>)mCurrentCommandBuffer;
    
    // 创建渲染通道描述符
    MTLRenderPassDescriptor* passDesc = [MTLRenderPassDescriptor renderPassDescriptor];
    passDesc.colorAttachments[0].texture = drawable.texture;
    passDesc.colorAttachments[0].loadAction = MTLLoadActionClear;
    passDesc.colorAttachments[0].storeAction = MTLStoreActionStore;
    passDesc.colorAttachments[0].clearColor = MTLClearColorMake(
        config.backgroundColor[0],
        config.backgroundColor[1],
        config.backgroundColor[2],
        config.backgroundColor[3]);
    
    // 创建渲染编码器
    id<MTLRenderCommandEncoder> encoder = [cmdBuffer renderCommandEncoderWithDescriptor:passDesc];
    
    // 设置渲染管线
    id<MTLRenderPipelineState> pipelineState = (__bridge id<MTLRenderPipelineState>)mRenderPipelineState;
    [encoder setRenderPipelineState:pipelineState];
    
    // 设置顶点缓冲
    id<MTLBuffer> vertexBuffer = (__bridge id<MTLBuffer>)mVertexBuffer;
    [encoder setVertexBuffer:vertexBuffer offset:0 atIndex:0];
    
    // 设置变换矩阵
    simd_float4x4 transformMatrix = matrix_identity_float4x4;
    if (config.flipHorizontal) {
        transformMatrix.columns[0][0] = -1.0f;
    }
    if (config.flipVertical) {
        transformMatrix.columns[1][1] = -1.0f;
    }
    [encoder setVertexBytes:&transformMatrix length:sizeof(transformMatrix) atIndex:1];
    
    // TODO: 设置纹理（从 LRTexture 获取 Metal 纹理）
    // id<MTLTexture> mtlTexture = ...;
    // [encoder setFragmentTexture:mtlTexture atIndex:0];
    
    // 设置采样器
    id<MTLSamplerState> sampler = (__bridge id<MTLSamplerState>)mSamplerState;
    [encoder setFragmentSamplerState:sampler atIndex:0];
    
    // 绘制
    [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
    
    [encoder endEncoding];
    
    return true;
}

bool iOSMetalSurface::endFrame() {
    if (mState != SurfaceState::Rendering) {
        return false;
    }
    
    if (!mCurrentDrawable || !mCurrentCommandBuffer) {
        return false;
    }
    
    id<CAMetalDrawable> drawable = (__bridge id<CAMetalDrawable>)mCurrentDrawable;
    id<MTLCommandBuffer> cmdBuffer = (__bridge id<MTLCommandBuffer>)mCurrentCommandBuffer;
    
    // 呈现
    [cmdBuffer presentDrawable:drawable];
    [cmdBuffer commit];
    
    // 释放当前帧资源
    CFRelease(mCurrentCommandBuffer);
    CFRelease(mCurrentDrawable);
    mCurrentCommandBuffer = nullptr;
    mCurrentDrawable = nullptr;
    
    mState = SurfaceState::Ready;
    return true;
}

void iOSMetalSurface::waitGPU() {
    if (mCurrentCommandBuffer) {
        id<MTLCommandBuffer> cmdBuffer = (__bridge id<MTLCommandBuffer>)mCurrentCommandBuffer;
        [cmdBuffer waitUntilCompleted];
    }
}

void iOSMetalSurface::setVSyncEnabled(bool enabled) {
    DisplaySurface::setVSyncEnabled(enabled);
    
    if (mMetalLayer) {
        CAMetalLayer* layer = (__bridge CAMetalLayer*)mMetalLayer;
        layer.displaySyncEnabled = enabled;
    }
}

// =============================================================================
// iOS 特定接口
// =============================================================================

void iOSMetalSurface::setMetalContextManager(IOSMetalContextManager* manager) {
    mMetalManager = manager;
}

void iOSMetalSurface::setColorSpace(void* colorSpace) {
    mColorSpace = colorSpace;
    
    if (mMetalLayer) {
        CAMetalLayer* layer = (__bridge CAMetalLayer*)mMetalLayer;
        layer.colorspace = (__bridge CGColorSpaceRef)colorSpace;
    }
}

// =============================================================================
// 内部方法
// =============================================================================

bool iOSMetalSurface::configureMetalLayer() {
    if (!mMetalLayer || !mDevice) {
        return false;
    }
    
    CAMetalLayer* layer = (__bridge CAMetalLayer*)mMetalLayer;
    id<MTLDevice> device = (__bridge id<MTLDevice>)mDevice;
    
    layer.device = device;
    layer.pixelFormat = (MTLPixelFormat)mPixelFormat;
    layer.framebufferOnly = YES;
    layer.displaySyncEnabled = mVSyncEnabled;
    
    if (mColorSpace) {
        layer.colorspace = (__bridge CGColorSpaceRef)mColorSpace;
    }
    
    return true;
}

bool iOSMetalSurface::acquireNextDrawable() {
    if (!mMetalLayer) {
        return false;
    }
    
    CAMetalLayer* layer = (__bridge CAMetalLayer*)mMetalLayer;
    id<CAMetalDrawable> drawable = [layer nextDrawable];
    
    if (!drawable) {
        PIPELINE_LOGW("Failed to acquire next drawable");
        return false;
    }
    
    mCurrentDrawable = (__bridge_retained void*)drawable;
    return true;
}

bool iOSMetalSurface::createRenderPipeline() {
    if (!mDevice) {
        return false;
    }
    
    id<MTLDevice> device = (__bridge id<MTLDevice>)mDevice;
    NSError* error = nil;
    
    // 编译 shader
    id<MTLLibrary> library = [device newLibraryWithSource:DISPLAY_SHADER_SOURCE
                                                  options:nil
                                                    error:&error];
    if (!library) {
        PIPELINE_LOGE("Failed to compile Metal shader: %s", 
                      [[error localizedDescription] UTF8String]);
        return false;
    }
    
    id<MTLFunction> vertexFunc = [library newFunctionWithName:@"displayVertex"];
    id<MTLFunction> fragmentFunc = [library newFunctionWithName:@"displayFragment"];
    
    // 创建顶点描述符
    MTLVertexDescriptor* vertexDesc = [[MTLVertexDescriptor alloc] init];
    vertexDesc.attributes[0].format = MTLVertexFormatFloat2;
    vertexDesc.attributes[0].offset = 0;
    vertexDesc.attributes[0].bufferIndex = 0;
    vertexDesc.attributes[1].format = MTLVertexFormatFloat2;
    vertexDesc.attributes[1].offset = sizeof(simd_float2);
    vertexDesc.attributes[1].bufferIndex = 0;
    vertexDesc.layouts[0].stride = sizeof(Vertex);
    vertexDesc.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
    
    // 创建渲染管线描述符
    MTLRenderPipelineDescriptor* pipelineDesc = [[MTLRenderPipelineDescriptor alloc] init];
    pipelineDesc.vertexFunction = vertexFunc;
    pipelineDesc.fragmentFunction = fragmentFunc;
    pipelineDesc.vertexDescriptor = vertexDesc;
    pipelineDesc.colorAttachments[0].pixelFormat = (MTLPixelFormat)mPixelFormat;
    
    // 创建管线状态
    id<MTLRenderPipelineState> pipelineState = [device newRenderPipelineStateWithDescriptor:pipelineDesc
                                                                                      error:&error];
    if (!pipelineState) {
        PIPELINE_LOGE("Failed to create render pipeline: %s",
                      [[error localizedDescription] UTF8String]);
        return false;
    }
    mRenderPipelineState = (__bridge_retained void*)pipelineState;
    
    // 创建采样器
    MTLSamplerDescriptor* samplerDesc = [[MTLSamplerDescriptor alloc] init];
    samplerDesc.minFilter = MTLSamplerMinMagFilterLinear;
    samplerDesc.magFilter = MTLSamplerMinMagFilterLinear;
    samplerDesc.sAddressMode = MTLSamplerAddressModeClampToEdge;
    samplerDesc.tAddressMode = MTLSamplerAddressModeClampToEdge;
    
    id<MTLSamplerState> sampler = [device newSamplerStateWithDescriptor:samplerDesc];
    mSamplerState = (__bridge_retained void*)sampler;
    
    // 创建顶点缓冲
    id<MTLBuffer> vertexBuffer = [device newBufferWithBytes:SCREEN_VERTICES
                                                     length:sizeof(SCREEN_VERTICES)
                                                    options:MTLResourceStorageModeShared];
    mVertexBuffer = (__bridge_retained void*)vertexBuffer;
    
    mResourcesInitialized = true;
    return true;
}

void iOSMetalSurface::cleanupResources() {
    if (mCurrentCommandBuffer) {
        CFRelease(mCurrentCommandBuffer);
        mCurrentCommandBuffer = nullptr;
    }
    
    if (mCurrentDrawable) {
        CFRelease(mCurrentDrawable);
        mCurrentDrawable = nullptr;
    }
    
    if (mRenderPipelineState) {
        CFRelease(mRenderPipelineState);
        mRenderPipelineState = nullptr;
    }
    
    if (mSamplerState) {
        CFRelease(mSamplerState);
        mSamplerState = nullptr;
    }
    
    if (mVertexBuffer) {
        CFRelease(mVertexBuffer);
        mVertexBuffer = nullptr;
    }
    
    mResourcesInitialized = false;
}

} // namespace ios
} // namespace output
} // namespace pipeline

#endif // __APPLE__
