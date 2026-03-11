/**
 * @file test_pipeline_new.cpp
 * @brief Pipeline 2.0 API 单元测试
 */

#include "pipeline/PipelineNew.h"
#include <iostream>
#include <cassert>

using namespace pipeline;

void test_builder_basic() {
    std::cout << "=== Test: Builder Basic ===" << std::endl;
    
    auto builder = Pipeline::create();
    builder.withPreset(PipelinePreset::CameraPreview)
           .withPlatform(PlatformType::iOS)
           .withResolution(1920, 1080);
    
    std::string error = builder.validate();
    assert(error.empty() && "Builder validation should pass");
    
    std::cout << "✓ Builder basic test passed" << std::endl;
}

void test_builder_validation() {
    std::cout << "=== Test: Builder Validation ===" << std::endl;
    
    auto builder = Pipeline::create();
    builder.withResolution(0, 1080);  // Invalid width
    
    std::string error = builder.validate();
    assert(!error.empty() && "Builder should detect invalid resolution");
    
    std::cout << "✓ Builder validation test passed" << std::endl;
}

void test_pipeline_creation() {
    std::cout << "=== Test: Pipeline Creation ===" << std::endl;
    
    auto pipeline = Pipeline::create()
        .withPreset(PipelinePreset::CameraPreview)
        .withPlatform(PlatformType::iOS)
        .withResolution(1920, 1080)
        .build();
    
    assert(pipeline != nullptr && "Pipeline should be created");
    assert(pipeline->getState() == PipelineState::Created && "Initial state should be Created");
    
    std::cout << "✓ Pipeline creation test passed" << std::endl;
}

void test_pipeline_state() {
    std::cout << "=== Test: Pipeline State ===" << std::endl;
    
    auto pipeline = Pipeline::create()
        .withPreset(PipelinePreset::CameraPreview)
        .withPlatform(PlatformType::iOS)
        .withResolution(1920, 1080)
        .build();
    
    assert(!pipeline->isRunning() && "Pipeline should not be running initially");
    
    // Note: We can't test start/stop without full initialization
    // which requires platform-specific setup
    
    std::cout << "✓ Pipeline state test passed" << std::endl;
}

void test_deprecated_facade() {
    std::cout << "=== Test: Deprecated Facade ===" << std::endl;
    
    // This should compile but generate deprecation warnings
    [[maybe_unused]] PipelineFacadeConfig config;
    config.preset = PipelinePreset::CameraPreview;
    config.platformConfig.platform = PlatformType::iOS;
    
    // Note: Creating PipelineFacade will generate deprecation warning
    // auto facade = PipelineFacade::create(config);
    
    std::cout << "✓ Deprecated facade types accessible" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Pipeline 2.0 Unit Tests" << std::endl;
    std::cout << "========================================" << std::endl << std::endl;
    
    try {
        test_builder_basic();
        test_builder_validation();
        test_pipeline_creation();
        test_pipeline_state();
        test_deprecated_facade();
        
        std::cout << std::endl << "========================================" << std::endl;
        std::cout << "All tests passed! ✓" << std::endl;
        std::cout << "========================================" << std::endl;
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}
