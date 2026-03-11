---
trigger: glob
globs: ["**/*.m", "**/*.mm", "**/*.h"]
name: objc-coding-standards
description: 基于 Apple Objective-C 编码规范、Google Objective-C Style Guide 与 Cocoa 最佳实践的编码标准。在编写、审查或重构 Objective-C / Objective-C++ 代码时使用，以强制实施安全、清晰和惯用的实践。
---

# Objective-C / Objective-C++ 编码标准

源自 [Apple Coding Guidelines for Cocoa](https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/CodingGuidelines/CodingGuidelines.html)、[Google Objective-C Style Guide](https://google.github.io/styleguide/objcguide.html) 与 Cocoa 社区最佳实践的综合编码标准。强制执行内存安全、命名清晰、接口最小化和 ARC 正确使用。

## 何时使用

* 编写新的 Objective-C 或 Objective-C++ 代码
* 审查或重构现有的 Objective-C 代码库
* 在混编（ObjC + Swift）项目中维护 Objective-C 层
* 在 Objective-C++ `.mm` 文件中混合使用 C++ 特性
* 封装 C/C++ 库供 Swift/ObjC 上层使用

### 何时不应使用

* 纯 Swift 项目（优先使用 swift_coding_rule）
* 无 Objective-C 运行时依赖的纯 C/C++ 模块

## 贯穿性原则

1. **清晰胜于简短**：方法名应完整描述行为，不要缩写
2. **ARC 为基础**：不再手动管理 `retain`/`release`；理解所有权语义
3. **最小化公开接口**：`.h` 只暴露必要内容；实现细节放入 `.m` 的类扩展
4. **防御性编程**：对 `nil` 消息发送是安全的；但返回值语义必须明确
5. **Nullability 注解**：所有公共 API 标注 `NS_ASSUME_NONNULL_BEGIN/END`
6. **协议驱动**：用 `@protocol` 解耦依赖，替代直接类耦合

---

## 命名规范

### 关键规则

| 规则 | 摘要 |
|------|------|
| **N.1** | 类名、协议名、枚举名、结构体名使用 `UpperCamelCase`，加项目前缀（2-3 个大写字母） |
| **N.2** | 方法名、属性名、变量名使用 `lowerCamelCase` |
| **N.3** | 常量（`const`、`#define`、`NS_ENUM`）使用 `前缀 + UpperCamelCase` |
| **N.4** | 方法名完整描述行为；每个参数前有介词标签 |
| **N.5** | 布尔属性以 `is`、`has`、`can`、`should` 开头 |
| **N.6** | 工厂方法以类名（小写）或 `+` 开头，返回类型明确 |
| **N.7** | 通知名以 `DidChange`、`WillChange`、`Did` 结尾，常量命名 |

```objc
// 正确：类名加前缀
@interface WXUserProfileViewController : UIViewController
@interface WXNetworkManager : NSObject
@protocol WXAuthenticatable <NSObject>

// N.2: 属性与方法 lowerCamelCase
@property (nonatomic, copy) NSString *userName;
@property (nonatomic, assign, getter=isActive) BOOL active;

- (void)fetchUserWithID:(NSInteger)userID completion:(void (^)(WXUser *user, NSError *error))completion;
- (BOOL)canPerformAction:(SEL)action withSender:(nullable id)sender;

// N.3: 常量
static NSString * const WXUserProfileDidUpdateNotification = @"WXUserProfileDidUpdate";
static const NSInteger WXMaxRetryCount = 3;

// N.6: 工厂方法
+ (instancetype)managerWithBaseURL:(NSURL *)baseURL;
+ (instancetype)defaultManager;

// 错误
@interface userVC : UIViewController     // N.1: 无前缀、非 UpperCamelCase
- (void)fetch:(int)id cb:(id)cb;         // N.4: 缩写、无参数标签
#define MAX_RETRY 3                      // N.3: 宏应改为常量
```

---

## 头文件与接口设计

### 关键规则

| 规则 | 摘要 |
|------|------|
| **H.1** | `.h` 仅暴露公共 API；内部属性和方法放入 `.m` 的匿名类扩展 |
| **H.2** | 使用 `@class` 前向声明替代 `#import`（减少编译依赖） |
| **H.3** | 所有公共头文件包裹在 `NS_ASSUME_NONNULL_BEGIN / END` 中 |
| **H.4** | 使用 `NS_UNAVAILABLE` / `NS_DESIGNATED_INITIALIZER` 明确初始化契约 |
| **H.5** | `instancetype` 替代 `id` 作为初始化方法和工厂方法的返回类型 |

```objc
// WXUserService.h — 公共接口最小化
#import <Foundation/Foundation.h>

@class WXUser;          // H.2: 前向声明
@protocol WXUserServiceDelegate;

NS_ASSUME_NONNULL_BEGIN  // H.3

@interface WXUserService : NSObject

@property (nonatomic, weak, nullable) id<WXUserServiceDelegate> delegate;

// H.5: instancetype
+ (instancetype)sharedService;
- (instancetype)initWithBaseURL:(NSURL *)baseURL NS_DESIGNATED_INITIALIZER;  // H.4
- (instancetype)init NS_UNAVAILABLE;  // H.4: 禁用默认 init

- (void)fetchUserWithID:(NSInteger)userID
             completion:(void (^)(WXUser * _Nullable user,
                                  NSError * _Nullable error))completion;

NS_ASSUME_NONNULL_END

// WXUserService.m — 内部实现细节
@interface WXUserService ()   // H.1: 匿名类扩展隐藏内部成员
@property (nonatomic, strong) NSURLSession *session;
@property (nonatomic, strong) NSCache *responseCache;
@property (nonatomic, copy)   NSURL *baseURL;
@end

@implementation WXUserService
// ...
@end
```

---

## 属性声明

### 关键规则

| 规则 | 摘要 |
|------|------|
| **Prop.1** | 始终显式声明属性特性：`nonatomic`/`atomic`、内存语义、`readonly`/`readwrite` |
| **Prop.2** | 对象类型用 `strong`（默认）或 `weak`（避免循环引用） |
| **Prop.3** | `NSString`、`NSArray`、`NSDictionary`、Block 属性用 `copy` |
| **Prop.4** | 基本类型（`int`、`BOOL`、`CGFloat`）用 `assign` |
| **Prop.5** | 委托（delegate）属性声明为 `weak` |
| **Prop.6** | 只读属性在 `.h` 声明 `readonly`，在类扩展中 `readwrite` |

```objc
// Prop.1 - Prop.6: 完整属性声明示例
@interface WXArticle : NSObject

// Prop.3: NSString 用 copy
@property (nonatomic, copy, readonly) NSString *title;
@property (nonatomic, copy, nullable) NSString *subtitle;

// Prop.2: 强引用子对象
@property (nonatomic, strong, readonly) NSArray<WXTag *> *tags;

// Prop.4: 基本类型 assign
@property (nonatomic, assign) NSInteger viewCount;
@property (nonatomic, assign, getter=isPublished) BOOL published;

// Prop.5: delegate weak
@property (nonatomic, weak, nullable) id<WXArticleDelegate> delegate;

// Prop.3: Block 用 copy
@property (nonatomic, copy, nullable) void (^onUpdate)(WXArticle *article);

@end

// Prop.6: 类扩展中 readwrite
@interface WXArticle ()
@property (nonatomic, copy, readwrite) NSString *title;
@property (nonatomic, strong, readwrite) NSArray<WXTag *> *tags;
@end
```

### 反模式

* `retain` 替代 `strong`（ARC 时代已废弃）
* `NSString` 属性用 `strong` 而非 `copy`（子类 `NSMutableString` 可被修改）
* 所有属性都声明为 `atomic`（性能低且通常不必要）

---

## 内存管理（ARC）

### 关键规则

| 规则 | 摘要 |
|------|------|
| **Mem.1** | 不要手动调用 `retain`、`release`、`autorelease`（ARC 已管理） |
| **Mem.2** | 用 `__weak` 打破 Block / delegate 引起的循环引用 |
| **Mem.3** | Block 捕获 `self` 时，使用 `weakSelf`/`strongSelf` 模式 |
| **Mem.4** | Core Foundation 对象用 `__bridge`、`CFBridgingRelease`、`CFBridgingRetain` 桥接 |
| **Mem.5** | 避免在 `dealloc` 中调用可能触发副作用的方法 |

```objc
// Mem.3: weakSelf / strongSelf 标准模式
- (void)startLoading {
    __weak typeof(self) weakSelf = self;
    [self.networkManager fetchData:^(NSData *data, NSError *error) {
        __strong typeof(weakSelf) strongSelf = weakSelf;
        if (!strongSelf) return;  // self 已释放，提前退出
        [strongSelf processData:data];
    }];
}

// Mem.2: delegate 用 weak 属性（见属性规则 Prop.5）

// Mem.4: CF 桥接
CFStringRef cfString = CFStringCreateWithCString(NULL, "hello", kCFStringEncodingUTF8);
NSString *nsString = CFBridgingRelease(cfString);  // 转移所有权给 ARC

CGColorRef cgColor = [UIColor redColor].CGColor;
// CGColor 不是 ObjC 对象，不受 ARC 管理，需手动保留
CGColorRetain(cgColor);
// ... 使用完后
CGColorRelease(cgColor);

// 错误
- (void)startLoading {
    [self.networkManager fetchData:^(NSData *data, NSError *error) {
        [self processData:data];  // Mem.3: 直接捕获 self，可能循环引用
    }];
}
```

---

## 空值处理与 Nullability

### 关键规则

| 规则 | 摘要 |
|------|------|
| **Nil.1** | 所有公共 API 文件包裹在 `NS_ASSUME_NONNULL_BEGIN/END` 中 |
| **Nil.2** | 可空参数/返回值显式标注 `nullable`；非空则依赖假设区域默认 nonnull |
| **Nil.3** | 向 `nil` 发送消息是安全的，但不要依赖此行为掩盖逻辑错误 |
| **Nil.4** | 方法返回 `nil` 时，应通过 `NSError **` 或文档明确说明原因 |
| **Nil.5** | 集合类型（`NSArray`、`NSDictionary`）使用泛型轻量注解 |

```objc
NS_ASSUME_NONNULL_BEGIN  // Nil.1

// Nil.2: 精确标注可空性
- (nullable WXUser *)userForID:(NSInteger)userID;
- (nullable NSData *)encodeUser:(WXUser *)user
                          error:(NSError * _Nullable *)error;  // Nil.4

// Nil.5: 泛型轻量注解
@property (nonatomic, copy) NSArray<WXUser *> *users;
@property (nonatomic, copy) NSDictionary<NSString *, WXConfig *> *configs;

NS_ASSUME_NONNULL_END

// Nil.3: 不要依赖 nil 消息安全性掩盖问题
// 错误（逻辑错误被掩盖）
NSString *name = nil;
NSUInteger length = name.length;  // 返回 0，静默失败
// 正确：先校验
NSAssert(name != nil, @"name must not be nil");
```

---

## Block 使用

### 关键规则

| 规则 | 摘要 |
|------|------|
| **Blk.1** | Block 类型用 `typedef` 命名，提升可读性 |
| **Blk.2** | 作为属性的 Block 必须使用 `copy` |
| **Blk.3** | Block 参数放在方法参数列表最后 |
| **Blk.4** | Block 内部用 `weakSelf`/`strongSelf` 避免循环引用（见 Mem.3） |
| **Blk.5** | 复杂 Block 内联时注意缩进对齐 |

```objc
// Blk.1: typedef 命名 Block 类型
typedef void (^WXCompletionBlock)(BOOL success, NSError * _Nullable error);
typedef void (^WXUserFetchBlock)(WXUser * _Nullable user, NSError * _Nullable error);

// Blk.3: Block 参数放最后
- (void)loginWithUsername:(NSString *)username
                 password:(NSString *)password
               completion:(WXCompletionBlock)completion;

- (void)fetchUsersInGroup:(NSString *)groupID
                    limit:(NSInteger)limit
               completion:(WXUserFetchBlock)completion;

// Blk.5: 内联 Block 缩进对齐
[self.service fetchUsersInGroup:@"admin"
                          limit:20
                     completion:^(WXUser * _Nullable user, NSError * _Nullable error) {
    if (error) {
        [self handleError:error];
        return;
    }
    [self updateUIWithUser:user];
}];

// 错误
@property (nonatomic, strong) void (^callback)(void);  // Blk.2: 应用 copy
```

---

## 协议（Protocol）

### 关键规则

| 规则 | 摘要 |
|------|------|
| **Pro.1** | 协议名以 `Delegate`、`DataSource`、`able` 后缀区分职责 |
| **Pro.2** | 可选方法用 `@optional`；必须实现的方法放入 `@required`（默认） |
| **Pro.3** | 调用可选委托方法前用 `respondsToSelector:` 检查 |
| **Pro.4** | 委托方法第一个参数传递发送方（`sender`） |
| **Pro.5** | 用协议替代继承解耦模块依赖 |

```objc
// Pro.1 + Pro.2 + Pro.4: 标准 Delegate 协议
@protocol WXDownloadManagerDelegate <NSObject>

@required
- (void)downloadManager:(WXDownloadManager *)manager
       didFinishWithData:(NSData *)data;

@optional
- (void)downloadManager:(WXDownloadManager *)manager
       didUpdateProgress:(float)progress;
- (void)downloadManager:(WXDownloadManager *)manager
        didFailWithError:(NSError *)error;

@end

// Pro.3: 调用前检查
- (void)notifyProgress:(float)progress {
    if ([self.delegate respondsToSelector:
            @selector(downloadManager:didUpdateProgress:)]) {
        [self.delegate downloadManager:self didUpdateProgress:progress];
    }
}
```

---

## 枚举与常量

### 关键规则

| 规则 | 摘要 |
|------|------|
| **En.1** | 使用 `NS_ENUM` 定义有序枚举，`NS_OPTIONS` 定义位掩码枚举 |
| **En.2** | 枚举名和 case 名使用 `前缀 + 类型名 + 值名` 全称 |
| **En.3** | 使用 `static const` 或 `extern NSString * const` 定义常量，避免 `#define` |
| **En.4** | 字符串常量声明在 `.h`（`extern`），定义在 `.m` |

```objc
// En.1 + En.2: NS_ENUM
typedef NS_ENUM(NSInteger, WXNetworkStatus) {
    WXNetworkStatusUnknown     = -1,
    WXNetworkStatusNotReachable = 0,
    WXNetworkStatusReachableWiFi,
    WXNetworkStatusReachableCellular,
};

// En.1: NS_OPTIONS 位掩码
typedef NS_OPTIONS(NSUInteger, WXPermission) {
    WXPermissionNone     = 0,
    WXPermissionRead     = 1 << 0,
    WXPermissionWrite    = 1 << 1,
    WXPermissionExecute  = 1 << 2,
};

// En.4: 字符串常量
// WXNotifications.h
extern NSString * const WXUserDidLoginNotification;
extern NSString * const WXUserDidLogoutNotification;

// WXNotifications.m
NSString * const WXUserDidLoginNotification  = @"WXUserDidLogin";
NSString * const WXUserDidLogoutNotification = @"WXUserDidLogout";

// 错误
#define WX_MAX_RETRY 3                  // En.3: 应用 static const
typedef enum { Red, Green, Blue } Color; // En.1: 应用 NS_ENUM
```

---

## 错误处理

### 关键规则

| 规则 | 摘要 |
|------|------|
| **Err.1** | 使用 `NSError **` 输出参数传递错误信息 |
| **Err.2** | 方法失败时返回 `nil` / `NO`，并填充 `NSError` |
| **Err.3** | 定义自己的错误域常量和错误码枚举 |
| **Err.4** | 异常（`@try/@throw`）仅用于编程错误，不用于业务逻辑流程控制 |
| **Err.5** | 调用返回 `NSError` 的方法后立即检查返回值，而非直接检查 error 对象 |

```objc
// Err.3: 自定义错误域
extern NSString * const WXNetworkErrorDomain;

typedef NS_ENUM(NSInteger, WXNetworkErrorCode) {
    WXNetworkErrorCodeTimeout      = 1001,
    WXNetworkErrorCodeUnauthorized = 1002,
    WXNetworkErrorCodeServerError  = 1003,
};

NSString * const WXNetworkErrorDomain = @"com.example.wx.network";

// Err.1 + Err.2: NSError 输出参数
- (nullable NSData *)fetchDataFromURL:(NSURL *)url
                                error:(NSError **)error {
    // ...
    if (requestFailed) {
        if (error) {
            *error = [NSError errorWithDomain:WXNetworkErrorDomain
                                         code:WXNetworkErrorCodeTimeout
                                     userInfo:@{
                NSLocalizedDescriptionKey: @"Request timed out",
                NSURLErrorFailingURLErrorKey: url
            }];
        }
        return nil;
    }
    return data;
}

// Err.5: 先检查返回值，再检查 error
NSError *error = nil;
NSData *data = [self fetchDataFromURL:url error:&error];
if (!data) {   // 正确：先检查返回值
    NSLog(@"Error: %@", error.localizedDescription);
    return;
}

// 错误
NSError *error = nil;
NSData *data = [self fetchDataFromURL:url error:&error];
if (error) {   // Err.5: 错误，应先检查返回值
    // ...
}
```

---

## Objective-C++ 混编（.mm）

### 关键规则

| 规则 | 摘要 |
|------|------|
| **MM.1** | `.mm` 文件中 C++ 对象不得暴露在 `.h` 头文件中（使用 PIMPL 或不透明指针） |
| **MM.2** | C++ 异常（`try/catch`）不要跨越 ObjC 边界传播 |
| **MM.3** | C++ 智能指针（`std::unique_ptr`、`std::shared_ptr`）可用于 `.mm` 的实现层 |
| **MM.4** | ObjC 对象传入 C++ 时注意 ARC 所有权语义与 `__bridge` 使用 |
| **MM.5** | 混编时确保头文件兼容纯 ObjC 编译（用 `#ifdef __cplusplus` 隔离 C++ 部分） |

```objc
// MM.1: PIMPL 隐藏 C++ 实现
// WXImageProcessor.h — 纯 ObjC 接口
NS_ASSUME_NONNULL_BEGIN
@interface WXImageProcessor : NSObject
- (instancetype)initWithConfig:(WXProcessorConfig *)config NS_DESIGNATED_INITIALIZER;
- (nullable UIImage *)processImage:(UIImage *)image error:(NSError **)error;
@end
NS_ASSUME_NONNULL_END

// WXImageProcessor.mm — C++ 实现细节
#import "WXImageProcessor.h"
#include <memory>
#include "CppImageEngine.h"

@interface WXImageProcessor () {
    std::unique_ptr<CppImageEngine> _engine;  // MM.3: C++ 智能指针在实现中
}
@end

@implementation WXImageProcessor
- (instancetype)initWithConfig:(WXProcessorConfig *)config {
    if (self = [super init]) {
        _engine = std::make_unique<CppImageEngine>(config.threshold);
    }
    return self;
}
@end

// MM.5: 头文件 C++ 隔离
#ifdef __cplusplus
#include <vector>
extern "C" {
#endif

void processRawData(const uint8_t *data, size_t length);

#ifdef __cplusplus
}
#endif
```

---

## 格式与代码风格

### 关键规则

| 规则 | 摘要 |
|------|------|
| **S.1** | 缩进使用 4 个空格，不使用 Tab |
| **S.2** | 每行最大 100 个字符 |
| **S.3** | 方法定义中 `-`/`+` 后加一个空格；参数标签与方法名对齐 |
| **S.4** | 逻辑块之间用空行分隔；`#pragma mark -` 组织代码段 |
| **S.5** | 实现文件顺序：`#pragma mark - Lifecycle` → `Public Methods` → `Private Methods` → `Delegate` |
| **S.6** | 使用 `clang-format` 或 Xcode 格式化工具保持一致 |

```objc
// S.3: 方法格式
- (void)configureWithUser:(WXUser *)user
                    theme:(WXTheme *)theme
               completion:(WXCompletionBlock)completion;

// S.4: pragma mark 组织
#pragma mark - Lifecycle

- (instancetype)initWithViewModel:(WXViewModel *)viewModel {
    // ...
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}

#pragma mark - Public Methods

- (void)reloadData { }

#pragma mark - Private Methods

- (void)setupSubviews { }
- (void)bindViewModel { }

#pragma mark - UITableViewDelegate

- (void)tableView:(UITableView *)tableView
    didSelectRowAtIndexPath:(NSIndexPath *)indexPath { }
```

---

## 快速参考检查清单

在标记 Objective-C 工作完成之前：

* [ ] 类名有项目前缀，使用 `UpperCamelCase` (N.1)
* [ ] `.h` 仅暴露公共 API；内部成员在类扩展中 (H.1)
* [ ] 公共头文件包裹在 `NS_ASSUME_NONNULL_BEGIN/END` (Nil.1, H.3)
* [ ] 可空返回值/参数已标注 `nullable` (Nil.2)
* [ ] `NSString`、Block、集合属性使用 `copy` (Prop.3)
* [ ] delegate 属性声明为 `weak` (Prop.5)
* [ ] Block 内捕获 `self` 使用 `weakSelf/strongSelf` 模式 (Mem.3)
* [ ] 没有手动 `retain`/`release`（ARC 项目）(Mem.1)
* [ ] CF 对象桥接使用 `__bridge` / `CFBridgingRelease` (Mem.4)
* [ ] 枚举使用 `NS_ENUM` / `NS_OPTIONS` (En.1)
* [ ] 常量使用 `static const` / `extern const`，避免 `#define` (En.3)
* [ ] 方法失败时返回 `nil`/`NO` 并填充 `NSError` (Err.2)
* [ ] 先检查方法返回值，再检查 `NSError` 对象 (Err.5)
* [ ] 可选委托方法调用前用 `respondsToSelector:` 检查 (Pro.3)
* [ ] `.mm` 中 C++ 类型不暴露在 `.h` 头文件（PIMPL）(MM.1)
* [ ] C++ 异常不跨越 ObjC 边界传播 (MM.2)
* [ ] 使用 `#pragma mark -` 组织实现文件 (S.4)
* [ ] `instancetype` 替代 `id` 作为初始化/工厂方法返回类型 (H.5)
