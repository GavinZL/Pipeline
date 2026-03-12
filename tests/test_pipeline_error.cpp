/**
 * @file test_pipeline_error.cpp
 * @brief Pipeline 错误处理系统单元测试
 */

#include "pipeline/core/PipelineError.h"
#include <iostream>
#include <cassert>

using namespace pipeline;

void test_error_creation() {
    std::cout << "=== Test: Error Creation ===" << std::endl;
    
    PipelineError err(1001, "Test error", ErrorCategory::InvalidArgument, "Try again");
    
    assert(err.hasError() == true);
    assert(err.code() == 1001);
    assert(err.message() == "Test error");
    assert(err.category() == ErrorCategory::InvalidArgument);
    assert(err.suggestion() == "Try again");
    
    std::cout << "✓ Error creation test passed" << std::endl;
}

void test_error_helpers() {
    std::cout << "=== Test: Error Helpers ===" << std::endl;
    
    auto err1 = PipelineError::invalidArgument("Bad param");
    assert(err1.category() == ErrorCategory::InvalidArgument);
    
    auto err2 = PipelineError::initializationFailed("Init failed");
    assert(err2.category() == ErrorCategory::Initialization);
    
    auto err3 = PipelineError::platformError("Platform error");
    assert(err3.category() == ErrorCategory::Platform);
    
    auto err4 = PipelineError::success();
    assert(err4.hasError() == false);
    
    std::cout << "✓ Error helpers test passed" << std::endl;
}

void test_result_void() {
    std::cout << "=== Test: Result<void> ===" << std::endl;
    
    Result<void> success = Result<void>::success();
    assert(success);
    assert(!success.hasError());
    
    Result<void> failure = Result<void>::error(PipelineError::runtimeError("Failed"));
    assert(!failure);
    assert(failure.hasError());
    assert(failure.error().code() == 5001);
    
    std::cout << "✓ Result<void> test passed" << std::endl;
}

void test_result_with_value() {
    std::cout << "=== Test: Result<T> ===" << std::endl;
    
    Result<int> success(42);
    assert(success);
    assert(success.value() == 42);
    
    Result<int> failure = Result<int>::error(PipelineError::invalidArgument("Bad input"));
    assert(!failure);
    assert(failure.hasError());
    
    std::cout << "✓ Result<T> test passed" << std::endl;
}

void test_error_to_string() {
    std::cout << "=== Test: Error toString ===" << std::endl;
    
    auto err = PipelineError::invalidArgument("Missing parameter");
    std::string str = err.toString();
    
    assert(str.find("InvalidArgument") != std::string::npos);
    assert(str.find("Missing parameter") != std::string::npos);
    
    std::cout << "✓ Error toString test passed" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Pipeline Error System Tests" << std::endl;
    std::cout << "========================================" << std::endl << std::endl;
    
    try {
        test_error_creation();
        test_error_helpers();
        test_result_void();
        test_result_with_value();
        test_error_to_string();
        
        std::cout << std::endl << "========================================" << std::endl;
        std::cout << "All error system tests passed! ✓" << std::endl;
        std::cout << "========================================" << std::endl;
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}
