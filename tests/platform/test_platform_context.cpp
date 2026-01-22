/**
 * @file test_platform_context.cpp
 * @brief PlatformContext 单元测试
 */

#include "pipeline/platform/PlatformContext.h"
#include <iostream>
#include <cassert>

using namespace pipeline;

void test_platform_detection() {
    std::cout << "=== Test: Platform Detection ===" << std::endl;
    
    PlatformContextConfig config;
    // 不指定平台，测试自动检测
    config.platform = PlatformType::Unknown;
    
    auto context = std::make_unique<PlatformContext>();
    bool success = context->initialize(config);
    
    assert(success && "Platform context initialization failed");
    assert(context->isInitialized() && "Context should be initialized");
    assert(context->getPlatformType() != PlatformType::Unknown && "Platform should be detected");
    
    std::cout << "Platform detected: " << (int)context->getPlatformType() << std::endl;
    std::cout << "Graphics API: " << (int)context->getGraphicsAPI() << std::endl;
    std::cout << "✓ Platform detection test passed" << std::endl << std::endl;
}

#ifdef __ANDROID__
void test_android_egl_context() {
    std::cout << "=== Test: Android EGL Context ===" << std::endl;
    
    PlatformContextConfig config;
    config.platform = PlatformType::Android;
    config.graphicsAPI = GraphicsAPI::OpenGLES;
    config.androidConfig.glesVersion = 3;
    config.androidConfig.offscreen = true;
    config.androidConfig.pbufferWidth = 16;
    config.androidConfig.pbufferHeight = 16;
    
    auto context = std::make_unique<PlatformContext>();
    bool success = context->initialize(config);
    
    assert(success && "Android EGL context initialization failed");
    
    // 测试上下文切换
    success = context->makeCurrent();
    assert(success && "makeCurrent failed");
    
    auto manager = context->getAndroidEGLManager();
    assert(manager != nullptr && "Android EGL manager should not be null");
    assert(manager->isCurrent() && "Context should be current");
    
    success = context->releaseCurrent();
    assert(success && "releaseCurrent failed");
    
    std::cout << "✓ Android EGL context test passed" << std::endl << std::endl;
}
#endif

#if defined(__APPLE__)
void test_ios_metal_context() {
    std::cout << "=== Test: iOS Metal Context ===" << std::endl;
    
    PlatformContextConfig config;
    config.platform = PlatformType::iOS;
    config.graphicsAPI = GraphicsAPI::Metal;
    config.iosConfig.enableTextureCache = true;
    
    auto context = std::make_unique<PlatformContext>();
    bool success = context->initialize(config);
    
    assert(success && "iOS Metal context initialization failed");
    
    auto manager = context->getIOSMetalManager();
    assert(manager != nullptr && "iOS Metal manager should not be null");
    assert(manager->getMetalDevice() != nullptr && "Metal device should be created");
    
    std::cout << "✓ iOS Metal context test passed" << std::endl << std::endl;
}
#endif

void test_lifecycle() {
    std::cout << "=== Test: Context Lifecycle ===" << std::endl;
    
    auto context = std::make_unique<PlatformContext>();
    
    // 初始化
    PlatformContextConfig config;
    bool success = context->initialize(config);
    assert(success && "Initialization failed");
    assert(context->isInitialized() && "Should be initialized");
    
    // 重复初始化应该成功（幂等性）
    success = context->initialize(config);
    assert(success && "Re-initialization should succeed");
    
    // 销毁
    context->destroy();
    assert(!context->isInitialized() && "Should not be initialized after destroy");
    
    // 重复销毁应该安全
    context->destroy();
    
    std::cout << "✓ Context lifecycle test passed" << std::endl << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "PlatformContext Unit Tests" << std::endl;
    std::cout << "========================================" << std::endl << std::endl;
    
    try {
        test_platform_detection();
        test_lifecycle();
        
#ifdef __ANDROID__
        test_android_egl_context();
#endif
        
#if defined(__APPLE__)
        test_ios_metal_context();
#endif
        
        std::cout << "========================================" << std::endl;
        std::cout << "All tests passed! ✓" << std::endl;
        std::cout << "========================================" << std::endl;
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}
