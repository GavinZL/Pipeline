/**
 * @file test_pipeline_json.cpp
 * @brief Pipeline JSON 配置系统单元测试
 */

#include "pipeline/core/PipelineConfigJson.h"
#include <iostream>
#include <cassert>
#include <fstream>
#include <sstream>

using namespace pipeline;

void test_json_load_from_string() {
    std::cout << "=== Test: Load from JSON String ===" << std::endl;
    
    const char* jsonStr = R"({
        "version": "2.0",
        "preset": "CameraPreview",
        "platform": {
            "type": "iOS"
        },
        "input": {
            "format": "RGBA",
            "width": 1920,
            "height": 1080
        },
        "quality": "High",
        "filters": [
            {
                "type": "beauty",
                "params": {
                    "smooth": 0.7,
                    "whiten": 0.3
                }
            }
        ]
    })";
    
    auto result = PipelineConfigLoader::fromString(jsonStr);
    assert(result && "Should load JSON config successfully");
    assert(result.value() != nullptr && "Pipeline should be created");
    
    std::cout << "✓ JSON string load test passed" << std::endl;
}

void test_json_load_from_file() {
    std::cout << "=== Test: Load from JSON File ===" << std::endl;
    
    // Create a test JSON file
    const char* jsonContent = R"({
        "version": "2.0",
        "preset": "CameraRecord",
        "platform": {
            "type": "Android"
        },
        "input": {
            "format": "NV12",
            "width": 1280,
            "height": 720
        },
        "quality": "Medium"
    })";
    
    const char* testFile = "/tmp/test_pipeline_config.json";
    std::ofstream file(testFile);
    file << jsonContent;
    file.close();
    
    auto result = PipelineConfigLoader::fromFile(testFile);
    assert(result && "Should load JSON file successfully");
    assert(result.value() != nullptr && "Pipeline should be created");
    
    std::remove(testFile);
    
    std::cout << "✓ JSON file load test passed" << std::endl;
}

void test_json_validation() {
    std::cout << "=== Test: JSON Validation ===" << std::endl;
    
    // Valid JSON
    nlohmann::json validJson = {
        {"version", "2.0"},
        {"preset", "Custom"}
    };
    assert(PipelineConfigLoader::validate(validJson) == true);
    
    // Invalid: missing version
    nlohmann::json invalidJson = {
        {"preset", "Custom"}
    };
    assert(PipelineConfigLoader::validate(invalidJson) == false);
    assert(!PipelineConfigLoader::getLastValidationError().empty());
    
    // Invalid: wrong version type
    nlohmann::json wrongTypeJson = {
        {"version", 123},
        {"preset", "Custom"}
    };
    assert(PipelineConfigLoader::validate(wrongTypeJson) == false);
    
    std::cout << "✓ JSON validation test passed" << std::endl;
}

void test_json_export() {
    std::cout << "=== Test: JSON Export ===" << std::endl;
    
    // Create a pipeline
    auto pipeline = Pipeline::create()
        .withPreset(PipelinePreset::CameraPreview)
        .withPlatform(PlatformType::iOS)
        .withResolution(1920, 1080)
        .build();
    
    assert(pipeline != nullptr);
    
    // Export to JSON
    auto jsonResult = PipelineConfigExporter::toJson(*pipeline);
    assert(jsonResult && "Should export to JSON successfully");
    
    std::string jsonStr = jsonResult.value();
    assert(!jsonStr.empty());
    assert(jsonStr.find("version") != std::string::npos);
    assert(jsonStr.find("2.0") != std::string::npos);
    
    std::cout << "✓ JSON export test passed" << std::endl;
}

void test_json_parse_error() {
    std::cout << "=== Test: JSON Parse Error ===" << std::endl;
    
    // Invalid JSON string
    std::string invalidJson = "{ invalid json }";
    
    auto result = PipelineConfigLoader::fromString(invalidJson);
    assert(!result && "Should fail with invalid JSON");
    assert(result.hasError());
    
    std::cout << "✓ JSON parse error test passed" << std::endl;
}

void test_json_platform_types() {
    std::cout << "=== Test: JSON Platform Types ===" << std::endl;
    
    const char* platforms[] = {"iOS", "Android", "macOS", "Windows", "Linux"};
    
    for (const char* platform : platforms) {
        std::ostringstream oss;
        oss << R"({
            "version": "2.0",
            "preset": "Custom",
            "platform": {
                "type": ")" << platform << R"("
            }
        })";
        
        auto result = PipelineConfigLoader::fromString(oss.str());
        assert(result && "Should accept valid platform type");
    }
    
    std::cout << "✓ JSON platform types test passed" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Pipeline JSON Config Tests" << std::endl;
    std::cout << "========================================" << std::endl << std::endl;
    
    try {
        test_json_load_from_string();
        test_json_load_from_file();
        test_json_validation();
        test_json_export();
        test_json_parse_error();
        test_json_platform_types();
        
        std::cout << std::endl << "========================================" << std::endl;
        std::cout << "All JSON tests passed! ✓" << std::endl;
        std::cout << "========================================" << std::endl;
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}
