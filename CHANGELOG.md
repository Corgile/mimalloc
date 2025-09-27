# 变更日志

本文件记录mimalloc内存跟踪增强项目的所有重要变更。

文档格式基于 [Keep a Changelog](https://keepachangelog.com/en/1.0.0/)，项目遵循 [语义版本控制](https://semver.org/spec/v2.0.0.html)。

## [Enhanced-3.0.9] - 2025-09-27

### 新增功能
- **增强内存跟踪系统**: 实现基于唯一事件的内存分配跟踪，解决内存池中指针重用识别问题
- **Event ID存储机制**: 在每个分配指针前添加8字节前缀空间，存储唯一且单调递增的事件ID
- **强制Padding增强**: 修改`MI_PADDING_SIZE`机制，无论是否启用debug模式都包含`MI_TRACK_PREFIX_SIZE`（8字节）
- **指针偏移逻辑**: 在分配和释放路径中实现指针算术，维持原始分配和用户可见指针的分离
- **自定义跟踪集成**: 添加`MI_TRACK_MYHOOK`系统，与现有mimalloc跟踪基础设施集成
- **基于分组的泄漏检测**: 实现基于调用栈的分组，用于复杂的内存泄漏分析和报告
- **原子事件计数器**: 使用原子操作的线程安全事件ID生成
- **测试套件**: 添加综合测试用例（`test-leak.c`）演示泄漏检测能力

### 核心修改
- **核心类型定义** (`include/mimalloc/types.h`):
  - 添加`MI_TRACK_PREFIX_SIZE = 8`常量用于事件元数据存储
  - 修改`MI_PADDING_SIZE`计算，始终包含8字节前缀：
    - 启用debug padding时: `sizeof(mi_padding_t) + MI_TRACK_PREFIX_SIZE`
    - 禁用debug padding时: `MI_TRACK_PREFIX_SIZE`
  - 更新`MI_PADDING_WSIZE`以包含额外的前缀空间
  - **关键**: 更新`MI_PAGES_DIRECT`数组大小计算，确保mimalloc内核数据结构与新padding机制兼容

- **小对象分配路径** (`src/alloc.c:mi_heap_malloc_small_zero`):
  - 添加指针偏移逻辑: `void* const p = (uint8_t*)raw_p + MI_TRACK_PREFIX_SIZE`
  - 修改为返回相对原始分配偏移`MI_TRACK_PREFIX_SIZE`的用户指针
  - 保持使用偏移指针调用`mi_track_malloc`

- **大对象分配路径** (`src/alloc.c:_mi_heap_malloc_zero_ex`):
  - 实现与小对象相同的指针偏移逻辑
  - 确保所有分配大小的一致行为
  - 在保持现有分配逻辑的同时添加跟踪层

- **本地释放路径** (`src/free.c:mi_free_generic_local`):
  - 添加指针转换: `void* raw_p = (uint8_t*)p - MI_TRACK_PREFIX_SIZE`
  - 将用户指针转换回原始分配指针进行处理
  - 保持与现有非对齐逻辑的兼容性

- **多线程释放路径** (`src/free.c:mi_free_generic_mt`):
  - 实现相同的指针转换逻辑用于线程安全操作
  - 确保跨线程模型的一致指针处理

- **跟踪钩子系统** (`include/mimalloc/track.h`):
  - 添加`MI_TRACK_MYHOOK`条件编译块
  - 定义自定义跟踪宏:
    - `mi_track_init()` → `my_track_init()`
    - `mi_track_malloc_size(p,req,size,z)` → `my_track_malloc(p,req,size,z)`
    - `mi_track_free_size(p,size)` → `my_track_free(p,size)`

### 实现细节

#### 事件跟踪系统 (`src/track/hiktrack.cc`)
- **EventRecord结构**: 存储event_id、backtrace_hash、分配大小和时间戳
- **EventMetadata结构**: 存储在指针前缀中的8字节结构，包含唯一event_id
- **全局状态管理**:
  - 线程安全的mutex保护操作
  - 从1开始的原子事件ID计数器
  - 用于活跃记录和调用栈分组的哈希表
- **关键函数**:
  - `my_track_malloc()`: 生成唯一event_id，存储在前缀中，创建跟踪记录
  - `my_track_free()`: 从前缀中检索event_id，验证并移除跟踪记录
  - `mi_print_leaks()`: 按调用栈（backtrace hash）分组报告内存泄漏

#### 架构优势
1. **唯一标识**: 每个分配都获得唯一event_id，不受指针地址重用影响
2. **最小性能影响**: 每次分配仅8字节开销 + 简单的指针算术开销
3. **现有基础设施利用**: 使用mimalloc经过验证的MI_PADDING机制和跟踪钩子
4. **向后兼容**: 不破坏现有mimalloc功能
5. **复杂分析**: 支持按调用栈分组相关分配进行泄漏分析

#### 关键设计决策
- **MI_PADDING_SIZE修改**: 确保所有分配路径自动包含前缀，无需大量代码更改
- **8字节前缀大小**: 足以容纳64位event_id并满足对齐要求
- **指针偏移策略**: 在添加跟踪层的同时保持mimalloc的分配/释放逻辑
- **原子事件生成**: 确保多线程环境中线程安全的唯一ID生成

#### MI_PADDING_WSIZE的关键作用
这是本项目最重要的技术细节之一。`MI_PADDING_WSIZE`宏直接影响mimalloc核心数据结构：

```c
#define MI_PADDING_WSIZE  ((MI_PADDING_SIZE + MI_INTPTR_SIZE - 1) / MI_INTPTR_SIZE)
#define MI_PAGES_DIRECT   (MI_SMALL_WSIZE_MAX + MI_PADDING_WSIZE + 1)
```

**修改前后对比**:
- **原始**: MI_PAGES_DIRECT = 128 + 0 + 1 = 129（无debug padding时）
- **修改后**: MI_PAGES_DIRECT = 128 + 1 + 1 = 130（强制8字节前缀）

这确保了`heap->pages_free_direct[MI_PAGES_DIRECT]`数组能正确处理所有可能的size class索引，避免数组越界访问。如果不同步更新MI_PADDING_WSIZE，会导致内存管理错误和系统崩溃。

### 测试验证
- **内存泄漏检测测试** (`test/test-leak.c`):
  - 演示不同大小的故意内存泄漏
  - 验证分配/释放周期中的事件跟踪
  - 显示按调用栈分组的泄漏报告
  - 确认与标准malloc/free接口的正确集成

### 技术说明
- 实现充分利用mimalloc现有的MI_PADDING基础设施以获得最大兼容性
- Event ID是单调递增的64位整数，提供几乎无限的唯一标识符
- 系统可以唯一标识分配事件，即使相同的指针地址被多次重用
- 调用栈分组支持识别泄漏模式和根本原因
- 所有修改都保持mimalloc的线程安全和性能特性

### 兼容性
- 保持与现有mimalloc API的完全向后兼容
- 禁用跟踪时不影响现有mimalloc用户的性能
- 可以通过`MI_TRACK_MYHOOK`编译标志启用/禁用自定义跟踪
- 在所有支持的mimalloc平台和配置上工作