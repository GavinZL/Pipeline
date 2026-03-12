/**
 * @file test_platform_strategy.cpp
 * @brief 平台策略模式单元测试
 */

#include "pipeline/platform/PlatformStrategy.h"
#include <iostream>
#include <cassert>

using namespace pipeline;
using namespace pipeline::strategy;

void test_strategy_factory() {
    std::cout << "=== Test: Strategy Factory ===" << std::endl;
    
    auto inputStrategy = PlatformStrategyFactory::createInputStrategy(
        PlatformType::iOS, input::InputFormat::RGBA);
    assert(inputStrategy != nullptr);
    assert(std::string(inputStrategy->getName()) == "GenericInputStrategy");
    
    auto outputStrategy = PlatformStrategyFactory::createOutputStrategy(PlatformType::iOS);
    assert(outputStrategy != nullptr);
    assert(std::string(outputStrategy->getName()) == "GenericOutputStrategy");
    
    std::cout << "✓ Strategy factory test passed" << std::endl;
}

void test_input_strategy_lifecycle() {
    std::cout << "=== Test: Input Strategy Lifecycle ===" << std::endl;
    
    auto strategy = PlatformStrategyFactory::createInputStrategy(
        PlatformType::Android, input::InputFormat::NV12);
    
    assert(strategy->initialize(nullptr, 1920, 1080) == true);
    
    uint8_t dummyData[100] = {0};
    assert(strategy->feedRGBA(dummyData, 100, 100, 0, 0) == true);
    assert(strategy->feedYUV420(dummyData, dummyData, dummyData, 100, 100, 0) == true);
    assert(strategy->feedNV12(dummyData, dummyData, 100, 100, false, 0) == true);
    
    strategy->destroy();
    
    std::cout << "✓ Input strategy lifecycle test passed" << std::endl;
}

void test_output_strategy_lifecycle() {
    std::cout << "=== Test: Output Strategy Lifecycle ===" << std::endl;
    
    auto strategy = PlatformStrategyFactory::createOutputStrategy(PlatformType::macOS);
    
    assert(strategy->initialize(nullptr) == true);
    
    int32_t displayId = strategy->createDisplayOutput(nullptr, 1920, 1080);
    assert(displayId > 0);
    
    int32_t callbackId = strategy->createCallbackOutput(
        [](const uint8_t*, size_t, uint32_t, uint32_t, int64_t) {}, 
        output::OutputFormat::RGBA);
    assert(callbackId > 0);
    
    strategy->setOutputEnabled(displayId, true);
    assert(strategy->removeOutput(displayId) == true);
    
    strategy->destroy();
    
    std::cout << "✓ Output strategy lifecycle test passed" << std::endl;
}

void test_cross_platform_strategy() {
    std::cout << "=== Test: Cross Platform Strategy ===" << std::endl;
    
    PlatformType platforms[] = {
        PlatformType::iOS,
        PlatformType::Android,
        PlatformType::macOS,
        PlatformType::Windows,
        PlatformType::Linux
    };
    
    for (auto platform : platforms) {
        auto input = PlatformStrategyFactory::createInputStrategy(platform, input::InputFormat::RGBA);
        auto output = PlatformStrategyFactory::createOutputStrategy(platform);
        
        assert(input != nullptr);
        assert(output != nullptr);
        
        assert(input->initialize(nullptr, 1280, 720));
        assert(output->initialize(nullptr));
        
        input->destroy();
        output->destroy();
    }
    
    std::cout << "✓ Cross platform strategy test passed" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Platform Strategy Unit Tests" << std::endl;
    std::cout << "========================================" << std::endl << std::endl;
    
    try {
        test_strategy_factory();
        test_input_strategy_lifecycle();
        test_output_strategy_lifecycle();
        test_cross_platform_strategy();
        
        std::cout << std::endl << "========================================" << std::endl;
        std::cout << "All strategy tests passed! ✓" << std::endl;
        std::cout << "========================================" << std::endl;
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}
