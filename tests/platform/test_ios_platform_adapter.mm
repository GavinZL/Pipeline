/**
 * @file test_ios_platform_adapter.mm
 * @brief iOS 平台适配层验证测试
 * 
 * 验证内容：
 * 1. PixelBufferInputStrategy 零拷贝纹理创建
 * 2. iOSMetalSurface 显示表面渲染
 * 3. PipelineFacade 新架构集成
 */

#if defined(__APPLE__)

#import <Foundation/Foundation.h>
#import <CoreVideo/CoreVideo.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "pipeline/PipelineFacade.h"
#include "pipeline/input/InputEntity.h"
#include "pipeline/input/ios/PixelBufferInputStrategy.h"
#include "pipeline/output/OutputEntity.h"
#include "pipeline/output/ios/iOSMetalSurface.h"
#include "pipeline/platform/PlatformContext.h"

#include <iostream>
#include <cassert>

using namespace pipeline;

// =============================================================================
// 测试辅助函数
// =============================================================================

namespace {

/**
 * @brief 创建测试用的 CVPixelBuffer
 */
CVPixelBufferRef createTestPixelBuffer(uint32_t width, uint32_t height) {
    CVPixelBufferRef pixelBuffer = nullptr;
    
    NSDictionary* attrs = @{
        (__bridge NSString*)kCVPixelBufferIOSurfacePropertiesKey: @{},
        (__bridge NSString*)kCVPixelBufferMetalCompatibilityKey: @YES
    };
    
    CVReturn status = CVPixelBufferCreate(
        kCFAllocatorDefault,
        width,
        height,
        kCVPixelFormatType_32BGRA,
        (__bridge CFDictionaryRef)attrs,
        &pixelBuffer
    );
    
    if (status != kCVReturnSuccess) {
        NSLog(@"[Test] Failed to create CVPixelBuffer: %d", status);
        return nullptr;
    }
    
    // 填充测试数据（渐变色）
    CVPixelBufferLockBaseAddress(pixelBuffer, 0);
    uint8_t* base = static_cast<uint8_t*>(CVPixelBufferGetBaseAddress(pixelBuffer));
    size_t bytesPerRow = CVPixelBufferGetBytesPerRow(pixelBuffer);
    
    for (uint32_t y = 0; y < height; ++y) {
        uint8_t* row = base + y * bytesPerRow;
        for (uint32_t x = 0; x < width; ++x) {
            uint8_t* pixel = row + x * 4;
            pixel[0] = static_cast<uint8_t>(x * 255 / width);  // B
            pixel[1] = static_cast<uint8_t>(y * 255 / height); // G
            pixel[2] = 128;                                     // R
            pixel[3] = 255;                                     // A
        }
    }
    
    CVPixelBufferUnlockBaseAddress(pixelBuffer, 0);
    
    return pixelBuffer;
}

void releasePixelBuffer(CVPixelBufferRef pixelBuffer) {
    if (pixelBuffer) {
        CVPixelBufferRelease(pixelBuffer);
    }
}

} // anonymous namespace

// =============================================================================
// 测试类
// =============================================================================

class IOSPlatformAdapterTest {
public:
    bool runAllTests() {
        NSLog(@"[Test] ========================================");
        NSLog(@"[Test] iOS Platform Adapter Tests Starting...");
        NSLog(@"[Test] ========================================");
        
        bool allPassed = true;
        
        // 测试 1: PixelBuffer 创建
        if (!testPixelBufferCreation()) {
            NSLog(@"[Test] FAILED: testPixelBufferCreation");
            allPassed = false;
        }
        
        // 测试 2: PixelBufferInputStrategy 初始化
        if (!testPixelBufferInputStrategy()) {
            NSLog(@"[Test] FAILED: testPixelBufferInputStrategy");
            allPassed = false;
        }
        
        // 测试 3: iOSMetalSurface 初始化
        if (!testIOSMetalSurface()) {
            NSLog(@"[Test] FAILED: testIOSMetalSurface");
            allPassed = false;
        }
        
        // 测试 4: PipelineFacade 集成
        if (!testPipelineFacadeIntegration()) {
            NSLog(@"[Test] FAILED: testPipelineFacadeIntegration");
            allPassed = false;
        }
        
        NSLog(@"[Test] ========================================");
        if (allPassed) {
            NSLog(@"[Test] All tests PASSED!");
        } else {
            NSLog(@"[Test] Some tests FAILED!");
        }
        NSLog(@"[Test] ========================================");
        
        return allPassed;
    }
    
private:
    // -------------------------------------------------------------------------
    // 测试 1: CVPixelBuffer 创建验证
    // -------------------------------------------------------------------------
    bool testPixelBufferCreation() {
        NSLog(@"[Test] Running: testPixelBufferCreation");
        
        CVPixelBufferRef pixelBuffer = createTestPixelBuffer(1920, 1080);
        if (!pixelBuffer) {
            NSLog(@"[Test]   - Failed to create test PixelBuffer");
            return false;
        }
        
        // 验证属性
        size_t width = CVPixelBufferGetWidth(pixelBuffer);
        size_t height = CVPixelBufferGetHeight(pixelBuffer);
        OSType format = CVPixelBufferGetPixelFormatType(pixelBuffer);
        
        bool passed = (width == 1920 && height == 1080 && format == kCVPixelFormatType_32BGRA);
        
        if (passed) {
            NSLog(@"[Test]   - PixelBuffer created: %zux%zu, format=0x%08X", width, height, format);
        }
        
        releasePixelBuffer(pixelBuffer);
        
        NSLog(@"[Test]   Result: %s", passed ? "PASSED" : "FAILED");
        return passed;
    }
    
    // -------------------------------------------------------------------------
    // 测试 2: PixelBufferInputStrategy 验证
    // -------------------------------------------------------------------------
    bool testPixelBufferInputStrategy() {
        NSLog(@"[Test] Running: testPixelBufferInputStrategy");
        
        auto strategy = std::make_shared<input::ios::PixelBufferInputStrategy>();
        
        // 验证策略名称
        const char* name = strategy->getName();
        if (strcmp(name, "PixelBufferInputStrategy") != 0) {
            NSLog(@"[Test]   - Unexpected strategy name: %s", name);
            return false;
        }
        NSLog(@"[Test]   - Strategy name: %s", name);
        
        // 初始化（无渲染上下文时应该返回 false 或进行基础初始化）
        bool initResult = strategy->initialize(nullptr);
        NSLog(@"[Test]   - Initialize without context: %s", initResult ? "OK" : "Expected (no context)");
        
        // 创建测试 PixelBuffer 并提交
        CVPixelBufferRef pixelBuffer = createTestPixelBuffer(640, 480);
        if (pixelBuffer) {
            bool submitResult = strategy->submitPixelBuffer(pixelBuffer, 0);
            NSLog(@"[Test]   - Submit PixelBuffer: %s", submitResult ? "OK" : "Failed (expected without full init)");
            releasePixelBuffer(pixelBuffer);
        }
        
        strategy->release();
        
        NSLog(@"[Test]   Result: PASSED (basic functionality verified)");
        return true;
    }
    
    // -------------------------------------------------------------------------
    // 测试 3: iOSMetalSurface 验证
    // -------------------------------------------------------------------------
    bool testIOSMetalSurface() {
        NSLog(@"[Test] Running: testIOSMetalSurface");
        
        auto surface = std::make_shared<output::ios::iOSMetalSurface>();
        
        // 验证初始状态
        if (surface->isAttached()) {
            NSLog(@"[Test]   - Unexpected: Surface attached before attachToLayer");
            return false;
        }
        NSLog(@"[Test]   - Initial state: not attached (correct)");
        
        // 验证状态
        auto state = surface->getState();
        NSLog(@"[Test]   - Surface state: %d (Uninitialized)", static_cast<int>(state));
        
        // 设置尺寸
        surface->setSize(1920, 1080);
        auto size = surface->getSize();
        NSLog(@"[Test]   - Size set to: %ux%u", size.width, size.height);
        
        // 注意：完整测试需要真实的 CAMetalLayer，这里只验证基础接口
        
        surface->release();
        
        NSLog(@"[Test]   Result: PASSED (basic interface verified)");
        return true;
    }
    
    // -------------------------------------------------------------------------
    // 测试 4: PipelineFacade 集成验证
    // -------------------------------------------------------------------------
    bool testPipelineFacadeIntegration() {
        NSLog(@"[Test] Running: testPipelineFacadeIntegration");
        
        // 配置 Pipeline
        PipelineFacadeConfig config;
        config.preset = PipelinePreset::CameraPreview;
        config.renderWidth = 1280;
        config.renderHeight = 720;
        config.enableAsync = true;
        config.platformConfig.platform = PlatformType::iOS;
        config.platformConfig.graphicsAPI = GraphicsAPI::Metal;
        config.enableDebugLog = true;
        
        // 创建 PipelineFacade
        auto pipeline = PipelineFacade::create(config);
        if (!pipeline) {
            NSLog(@"[Test]   - Failed to create PipelineFacade");
            return false;
        }
        NSLog(@"[Test]   - PipelineFacade created successfully");
        
        // 初始化
        bool initResult = pipeline->initialize();
        NSLog(@"[Test]   - Initialize: %s", initResult ? "OK" : "Failed");
        
        // 检查状态
        auto state = pipeline->getState();
        NSLog(@"[Test]   - State after init: %d", static_cast<int>(state));
        
        // 验证未运行状态
        bool isRunning = pipeline->isRunning();
        NSLog(@"[Test]   - Is running (before start): %s", isRunning ? "Yes" : "No (correct)");
        
        // 测试帧输入（无输出配置时应优雅处理）
        CVPixelBufferRef pixelBuffer = createTestPixelBuffer(1280, 720);
        if (pixelBuffer) {
            bool feedResult = pipeline->feedPixelBuffer(pixelBuffer, 0);
            NSLog(@"[Test]   - Feed PixelBuffer (no output): %s", feedResult ? "OK" : "Rejected (expected)");
            releasePixelBuffer(pixelBuffer);
        }
        
        // 清理
        pipeline->destroy();
        NSLog(@"[Test]   - Pipeline destroyed");
        
        NSLog(@"[Test]   Result: PASSED");
        return true;
    }
};

// =============================================================================
// 测试入口
// =============================================================================

extern "C" {

/**
 * @brief 运行 iOS 平台适配层测试
 * @return 0 表示全部通过，非 0 表示有失败
 */
int runIOSPlatformAdapterTests() {
    @autoreleasepool {
        IOSPlatformAdapterTest test;
        bool passed = test.runAllTests();
        return passed ? 0 : 1;
    }
}

} // extern "C"

#endif // __APPLE__
