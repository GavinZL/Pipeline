# Pipeline AI Agent Configuration

This file defines coding standards and build commands for AI agents working in the Pipeline C++ video processing framework.

---

## Build Commands

### Basic Build (CMake)
```bash
# Configure (Release)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Configure (Debug with symbols)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug

# Build Pipeline library
cmake --build build --config Release --target Pipeline

# Platform-specific scripts
./script/build_macos.sh -c Release      # macOS
./script/build_macos.sh -c Debug        # macOS debug
./script/build_ios.sh                   # iOS
./script/build_android.sh               # Android
```

### Testing
```bash
# Configure with tests enabled
cmake -S . -B build -DPIPELINE_BUILD_TESTS=ON

# Build tests
cmake --build build --target test_platform_context

# Run a single test
./build/test_platform_context

# Run via CTest
ctest -R PlatformContextTest --verbose
```

### IDE Integration
```bash
# Generate Xcode project
./script/generate_xcode_project.sh

# Generate compile_commands.json for clangd
export CMAKE_EXPORT_COMPILE_COMMANDS=ON
```

---

## Code Style Guidelines

### Language & Standards
- **C++ Standard**: C++17 (minimum)
- **File Extensions**: `.cpp` for source, `.h` for headers, `.mm` for Objective-C++
- **Encoding**: UTF-8 with Unix line endings (LF)

### Naming Conventions
```cpp
// Classes: PascalCase
class ProcessEntity { };
class PipelineManager { };

// Methods/Functions: camelCase
void setEnabled(bool enabled);
bool areInputsReady() const;

// Member Variables: mCamelCase with m prefix
std::string mName;
std::atomic<bool> mEnabled{true};
std::mutex mPortsMutex;

// Static Members: sCamelCase with s prefix
static std::atomic<EntityId> sNextId{1000};

// Constants: kPascalCase with k prefix
const int kDefaultBufferSize = 1024;

// Types: PascalCase with descriptive suffix
using FramePacketPtr = std::shared_ptr<FramePacket>;
enum class EntityState { Idle, Ready, Processing };

// Macros/Defines: UPPER_SNAKE_CASE with namespace prefix
#define PIPELINE_PLATFORM_IOS
```

### Header File Structure
```cpp
/**
 * @file ProcessEntity.h
 * @brief 处理节点基类 - 管线中的核心处理单元
 */

#pragma once

// 1. Project headers (quoted)
#include "pipeline/data/EntityTypes.h"

// 2. Standard library headers (angled)
#include <string>
#include <vector>
#include <memory>

namespace pipeline {

// Forward declarations
class PipelineContext;

/**
 * @brief Entity配置参数
 */
struct EntityConfig {
    std::string name;
    bool enabled = true;
};

/**
 * @brief 处理节点基类
 * 
 * ProcessEntity是管线中的核心处理单元...
 */
class ProcessEntity : public std::enable_shared_from_this<ProcessEntity> {
public:
    explicit ProcessEntity(const std::string& name = "");
    virtual ~ProcessEntity();
    
    // Disable copy
    ProcessEntity(const ProcessEntity&) = delete;
    ProcessEntity& operator=(const ProcessEntity&) = delete;
    
    // Methods grouped by functionality with section comments
    // ==========================================================================
    // 端口管理
    // ==========================================================================
    InputPort* addInputPort(const std::string& name);
    
protected:
    // Subclass interface
    virtual bool process(const std::vector<FramePacketPtr>& inputs,
                        std::vector<FramePacketPtr>& outputs,
                        PipelineContext& context) = 0;
    
private:
    EntityId mId;
    std::string mName;
    std::atomic<EntityState> mState{EntityState::Idle};
};

} // namespace pipeline
```

### Implementation File Structure
```cpp
/**
 * @file ProcessEntity.cpp
 * @brief ProcessEntity实现
 */

#include "pipeline/entity/ProcessEntity.h"
#include "pipeline/core/PipelineConfig.h"
#include <chrono>
#include <algorithm>

namespace pipeline {

// Static member initialization
std::atomic<EntityId> ProcessEntity::sNextId{1000};

ProcessEntity::ProcessEntity(const std::string& name)
    : mId(sNextId.fetch_add(1))
    , mName(name.empty() ? "Entity_" + std::to_string(mId) : name)
{
}

// =============================================================================
// 端口管理
// =============================================================================

InputPort* ProcessEntity::addInputPort(const std::string& name) {
    std::lock_guard<std::mutex> lock(mPortsMutex);
    // ...
}

} // namespace pipeline
```

### Error Handling
```cpp
// Use exceptions for exceptional cases only
// Prefer return values with std::optional or bool for expected failures

// ✅ Good: Return false for expected failure
bool ProcessEntity::execute(PipelineContext& context) {
    if (!areInputsReady()) {
        setState(EntityState::Blocked);
        return false;
    }
    // ...
}

// ✅ Good: Use optional for values that may not exist
std::optional<T> getParameter(const std::string& key) const;

// ✅ Good: Always use try-catch with shared_from_this()
try {
    auto self = shared_from_this();
    mCallback(self);
} catch (const std::bad_weak_ptr& e) {
    LOGE("shared_from_this() failed: %s", e.what());
    mCallback(nullptr);
}

// ❌ Bad: Suppressing errors
catch (...) {}  // Never do this
```

### Thread Safety
```cpp
// Use std::atomic for simple types
std::atomic<bool> mEnabled{true};
std::atomic<uint64_t> mProcessCount{0};

// Use mutex for complex data structures
mutable std::mutex mPortsMutex;
std::vector<std::unique_ptr<Port>> mPorts;

// Lock scope should be minimal
InputPort* ProcessEntity::getInputPort(size_t index) const {
    std::lock_guard<std::mutex> lock(mPortsMutex);
    if (index < mInputPorts.size()) {
        return mInputPorts[index].get();
    }
    return nullptr;
}

// Mark const methods that need mutex locking with 'mutable' mutex
bool areInputsReady() const;  // uses mutable std::mutex mPortsMutex
```

### Architecture Compliance (CRITICAL)

#### 1. Async Task Chain Architecture
```cpp
// ✅ Correct: Non-blocking check
bool MyEntity::execute(PipelineContext& context) {
    if (!areInputsReady()) {
        return false;  // Return immediately, wait for next schedule
    }
    // ...
}

// ❌ Wrong: Blocking wait
bool MyEntity::execute(PipelineContext& context) {
    if (!waitInputsReady(-1)) return false;  // BLOCKS!
    // ...
}
```

#### 2. Pipeline Initialization Order (MUST FOLLOW)
```cpp
// Correct sequence:
create(config) → initialize() → setupDisplayOutput() → start() → feedPixelBuffer()

// ❌ Wrong: setupDisplayOutput before initialize
auto pipeline = PipelineFacade::create(config);
pipeline->setupDisplayOutput(...);  // mPipelineManager is null!
pipeline->initialize();  // Too late
```

---

## Architecture Rules (from .qoder/rules/architecture-guidelines.md)

- **Rule trigger**: always_on
- **Core principles**:
  1. Async task chain - non-blocking execute() methods
  2. Queue precheck optimization - check queue before condition variable wait
  3. Pipeline init order: create → initialize → setupDisplayOutput → start
  4. Platform-specific: Metal texture storage modes, CVPixelBuffer handling
  5. Thread safety: try-catch around shared_from_this()

---

## Testing Guidelines

### Test File Naming
- `test_<module_name>.cpp` for unit tests
- Platform-specific: `test_ios_platform_adapter.mm`

### Test Structure
```cpp
void test_feature_name() {
    std::cout << "=== Test: Feature Name ===" << std::endl;
    
    // Setup
    auto context = std::make_unique<PlatformContext>();
    
    // Execute & Assert
    bool success = context->initialize(config);
    assert(success && "Initialization failed");
    
    std::cout << "✓ Test passed" << std::endl;
}

int main() {
    try {
        test_feature_name();
        std::cout << "All tests passed! ✓" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << std::endl;
        return 1;
    }
}
```

---

## File Organization

```
Pipeline/
├── include/pipeline/       # Public headers
│   ├── entity/             # Entity base classes
│   ├── core/               # Pipeline core
│   ├── data/               # Data structures
│   ├── input/              # Input entities
│   ├── output/             # Output entities
│   ├── platform/           # Platform abstraction
│   └── utils/              # Utilities
├── src/                    # Implementation files (mirror include structure)
├── entities/               # Example entity implementations
│   ├── gpu/                # GPU entities
│   └── cpu/                # CPU entities
├── tests/                  # Unit tests
├── third_party/            # Git submodules
│   ├── LREngine/           # Rendering engine
│   ├── TaskQueue/          # Task queue
│   └── libyuv/             # YUV conversion
└── script/                 # Build scripts
```

---

## Key Documentation References

| Document | Purpose |
|----------|---------|
| `.qoder/rules/architecture-guidelines.md` | Architecture compliance (always_on) |
| `ARCHITECTURE_DESIGN.md` | High-level architecture |
| `API_QUICK_REFERENCE.md` | API usage patterns |
| `docs/ASYNC_TASK_DRIVEN_DESIGN.md` | Async task chain design |

---

**Version**: v2.0  
**Last Updated**: 2026-03-10  
**Maintainer**: Pipeline Team
</content>