# Phase 3: 错误处理现代化方案

> **Phase**: 3 / 6  
> **目标**: 用 Result<T> 替代 bool 返回值，提供详细错误信息  
> **语言标准**: C++17（不使用 C++23 std::expected）  
> **状态**: 已完成

---

## 一、问题分析

### 1.1 当前痛点

**原有代码使用 bool 返回值：**
```cpp
bool initialize();  // 失败原因未知
bool feedRGBA(...); // 无法区分错误类型
bool start();       // 无法提供恢复建议
```

**问题：**
- ❌ 错误信息丢失
- ❌ 无法分类错误
- ❌ 无法提供恢复建议
- ❌ 调试困难

### 1.2 重构目标

1. **类型安全**：编译期检查错误处理
2. **详细错误信息**：错误码 + 消息 + 类别 + 恢复建议
3. **链式错误传播**：避免嵌套 if 判断
4. **向后兼容**：保留旧 API（deprecated）

---

## 二、设计方案

### 2.1 错误类别枚举

```cpp
enum class ErrorCategory : uint8_t {
    None,            // 无错误 (0)
    InvalidArgument, // 无效参数 (1000)
    Initialization,  // 初始化失败 (2000)
    Platform,        // 平台错误 (3000)
    Resource,        // 资源错误 (4000)
    Runtime,         // 运行时错误 (5000)
    NotSupported,    // 不支持的操作 (6000)
    Internal         // 内部错误 (9000)
};
```

**错误码分配：**
| 范围 | 类别 |
|------|------|
| 0 | 无错误 |
| 1000-1999 | 参数错误 |
| 2000-2999 | 初始化错误 |
| 3000-3999 | 平台错误 |
| 4000-4999 | 资源错误 |
| 5000-5999 | 运行时错误 |
| 6000-6999 | 不支持的操作 |
| 9000-9999 | 内部错误 |

### 2.2 PipelineError 类

```cpp
class PipelineError {
public:
    // 构造函数
    PipelineError(int code, std::string message, 
                  ErrorCategory category = ErrorCategory::Internal,
                  std::optional<std::string> suggestion = std::nullopt);
    
    // 查询方法
    bool hasError() const;                    // 是否有错误
    int code() const;                         // 错误码
    const std::string& message() const;       // 错误消息
    ErrorCategory category() const;           // 错误类别
    std::string suggestion() const;           // 恢复建议
    std::string toString() const;             // 完整错误信息
    
    // 预定义错误工厂方法
    static PipelineError success();
    static PipelineError invalidArgument(const std::string& details);
    static PipelineError initializationFailed(const std::string& details);
    static PipelineError platformError(const std::string& details);
    static PipelineError resourceError(const std::string& details);
    static PipelineError runtimeError(const std::string& details);
    static PipelineError notSupported(const std::string& details);
    static PipelineError internalError(const std::string& details);
};
```

**使用示例：**
```cpp
auto result = pipeline->start();
if (!result) {
    auto error = result.error();
    std::cerr << "[" << error.code() << "] " 
              << error.message() << std::endl;
    std::cerr << "建议: " << error.suggestion() << std::endl;
}
```

### 2.3 Result<T> 模板类

**C++17 实现（替代 C++23 std::expected）：**

```cpp
template<typename T>
class Result {
public:
    // 构造成功结果
    Result(T value);
    
    // 构造错误结果
    Result(PipelineError error);
    
    // 禁止拷贝，允许移动
    Result(Result&&) noexcept;
    Result& operator=(Result&&) noexcept;
    
    // 布尔转换 - 检查是否成功
    explicit operator bool() const;
    
    // 访问值
    bool hasValue() const;
    T& value() &;
    const T& value() const &;
    T&& value() &&;
    
    // 访问错误
    bool hasError() const;
    const PipelineError& error() const;
    
    // 静态工厂方法
    static Result success(T value);
    static Result error(PipelineError err);
};
```

**void 特化：**
```cpp
template<>
class Result<void> {
public:
    Result();  // 成功
    Result(PipelineError error);
    
    explicit operator bool() const;
    bool hasError() const;
    const PipelineError& error() const;
    
    static Result success();
    static Result error(PipelineError err);
};
```

### 2.4 错误处理模式

**模式 1: 直接检查**
```cpp
auto result = pipeline->start();
if (!result) {
    handleError(result.error());
    return;
}
```

**模式 2: 链式传播**
```cpp
Result<void> init() {
    auto r1 = setupInput();
    if (!r1) return r1;  // 自动传播错误
    
    auto r2 = setupOutput();
    if (!r2) return r2;
    
    return Result<void>::success();
}
```

**模式 3: 使用辅助宏**
```cpp
#define RETURN_IF_ERROR(result) \
    do { \
        auto _res = (result); \
        if (!_res) return Result<void>::error(_res.error()); \
    } while(0)

Result<void> init() {
    RETURN_IF_ERROR(setupInput());
    RETURN_IF_ERROR(setupOutput());
    return Result<void>::success();
}
```

---

## 三、接口变更

### 3.1 Pipeline 类变更

| 旧接口 | 新接口 |
|--------|--------|
| `bool start()` | `Result<void> start()` |
| `bool feedRGBA(...)` | `Result<void> feedRGBA(...)` |
| `bool feedYUV420(...)` | `Result<void> feedYUV420(...)` |
| `bool feedNV12(...)` | `Result<void> feedNV12(...)` |
| `bool feedTexture(...)` | `Result<void> feedTexture(...)` |

### 3.2 PipelineImpl 类变更

| 旧接口 | 新接口 |
|--------|--------|
| `bool feedRGBA(...)` | `Result<void> feedRGBA(...)` |
| `bool feedYUV420(...)` | `Result<void> feedYUV420(...)` |
| `bool feedNV12(...)` | `Result<void> feedNV12(...)` |
| `bool feedTexture(...)` | `Result<void> feedTexture(...)` |

---

## 四、实现细节

### 4.1 文件结构

```
Pipeline/
├── include/pipeline/core/
│   └── PipelineError.h          # 错误类和 Result 模板
├── src/core/
│   └── PipelineError.cpp        # 实现
└── tests/
    └── test_pipeline_error.cpp  # 单元测试
```

### 4.2 关键代码实现

**PipelineError::toString():**
```cpp
std::string PipelineError::toString() const {
    std::ostringstream oss;
    oss << "[" << categoryToString(m_category) << "]"
        << " (Code: " << m_code << ") "
        << m_message;
    if (m_suggestion) {
        oss << " | Suggestion: " << *m_suggestion;
    }
    return oss.str();
}
```

**预定义错误工厂:**
```cpp
PipelineError PipelineError::invalidArgument(const std::string& details) {
    return PipelineError(
        1001,
        "Invalid argument: " + details,
        ErrorCategory::InvalidArgument,
        "Check the parameter values and try again"
    );
}
```

### 4.3 错误转换

从旧代码迁移到新系统：

```cpp
// 旧代码
bool PipelineImpl::feedRGBA(...) {
    if (mInputStrategy) {
        return mInputStrategy->feedRGBA(...);
    }
    return false;  // 信息丢失
}

// 新代码
Result<void> PipelineImpl::feedRGBA(...) {
    if (!mInputStrategy) {
        return Result<void>::error(
            PipelineError::internalError("Input strategy not initialized"));
    }
    if (!mInputStrategy->feedRGBA(...)) {
        return Result<void>::error(
            PipelineError::runtimeError("Failed to feed RGBA data"));
    }
    return Result<void>::success();
}
```

---

## 五、测试策略

### 5.1 测试覆盖

| 测试项 | 说明 |
|--------|------|
| 错误创建 | 验证所有属性和方法 |
| 错误辅助函数 | 验证预定义错误工厂 |
| Result<void> | 成功/失败场景 |
| Result<T> | 带值返回场景 |
| 错误转换 | toString() 方法 |

### 5.2 测试示例

```cpp
void test_result_void() {
    Result<void> success = Result<void>::success();
    assert(success);
    assert(!success.hasError());
    
    Result<void> failure = Result<void>::error(
        PipelineError::runtimeError("Failed"));
    assert(!failure);
    assert(failure.hasError());
    assert(failure.error().code() == 5001);
}
```

---

## 六、验收标准

- [x] PipelineError 类可用
- [x] Result<T> 模板可用（C++17）
- [x] 所有 feed 方法返回 Result<void>
- [x] start() 方法返回 Result<void>
- [x] 错误包含详细信息和恢复建议
- [x] 所有测试通过

---

## 七、迁移指南

### 7.1 从旧代码迁移

**步骤 1: 修改接口返回类型**
```cpp
// 从
bool myFunction();

// 到
Result<void> myFunction();
```

**步骤 2: 修改实现**
```cpp
// 从
return false;

// 到
return Result<void>::error(PipelineError::runtimeError("Reason"));
```

**步骤 3: 修改调用点**
```cpp
// 从
if (!pipeline->feedRGBA(...)) { /* 不知道原因 */ }

// 到
auto result = pipeline->feedRGBA(...);
if (!result) {
    std::cerr << result.error().message() << std::endl;
}
```

---

## 八、参考文档

- C++ Core Guidelines: Error Handling
- isocpp.org/wiki/faq/exceptions
- Pipeline API Quick Reference (API_QUICK_REFERENCE.md)

---

**创建日期**: 2026-03-11  
**维护者**: Pipeline Team
