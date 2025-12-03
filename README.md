# Concurrent-Memory-Pool

[![Language](https://img.shields.io/badge/language-C/C++-blue.svg)]()
[![License](https://img.shields.io/badge/license-MIT-green.svg)]()
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-lightgrey.svg)]()
[![Build](https://img.shields.io/badge/build-passing-brightgreen.svg)]()

基于 C++ 的高并发内存池

一个 **参考 tcmalloc 设计思想**、使用 **C++** 自主实现的 **高性能、高并发内存池（Concurrent Memory Pool）**。

采用 ThreadCache → CentralCache → PageCache 的三级内存管理体系，支持：

* **线程无锁小对象分配**
* **批量分配与批量回收**
* **页级大块内存管理**
* **跨线程高速缓存共享**
* **大对象（>256KB）直通 PageCache / 系统堆**
* **基数树（Radix Tree）优化页号映射查找速度**

性能测试显示：在多线程环境下，**内存池的速度可达 malloc 的 2 ~ 6 倍**。

开发环境：**Visual Studio 2019 + MSVC + C/C++**


## 🚀 项目简介


### 1️⃣ ThreadCache —— 线程级无锁分配路径

* 每个线程通过 TLS 拥有独立的 ThreadCache
* 小对象分配无需加锁，延迟极低
* 哈希桶（FreeList）根据对齐规则管理多个尺寸段
* 引入“慢开始反馈调节算法”动态调整批量申请数量


### 2️⃣ CentralCache —— 多线程共享对象中心

* 每个 FreeList 桶带独立互斥锁，减少锁竞争
* 将多个对象批量提供给 ThreadCache，提高整体吞吐量
* 管理从 PageCache 切分出的 Span
* 负责小对象回收和 Span 状态维护

### 3️⃣ PageCache —— 页级大块内存管理（类似 Linux 页框分配器）

* 管理 K 页连续内存（默认为 8KB 一页）
* 可以从 ≥K 页的 Span 中切分
* 支持前后页合并（类似 buddy system）
* 大块内存（>256KB）直接从 PageCache 或系统堆申请
* 用 PageMap（基数树）映射页号 → Span，提高查找效率

### 4️⃣ 基数树（Radix Tree）优化

使用一层或两层基数树替换 unordered_map：

* 查找时间 O(1)，无需加锁
* 空间连续，CPU cache 命中更高
* 大幅减少 PageCache 中的锁竞争

### 5️⃣ ObjectPool —— 定长内存池消除 new/delete

Span、ThreadCache、SpanList 头节点等内部对象全部用：

* 定长内存池 ObjectPool 管理
* 避免 new/delete → 避免 malloc
* 提升 allocator 的自举性能（self-hosting）

### 6️⃣ 完整的多线程 Benchmark

支持：

* 固定尺寸对象压力测试
* 随机尺寸对象压力测试
* 各线程独立申请+释放
* 最终性能对比输出

真实展示线程竞争、多级缓存命中率、锁效率等现象。


## 📚 项目涉及的技术点

该项目系统性涉及了 C++ / 操作系统 / 数据结构 / 高性能并发编程核心技术：

🔹 **C/C++ 语言与 C++11 特性**

* 指针运算、placement new
* 对齐、内存模型
* RAII（std::unique_lock）
* TLS（thread_local）
* 单例模式（Meyers Singleton）

🔹 **数据结构**

* 链表（SpanList / FreeList）
* 基数树（Radix Tree）
* 哈希桶（多尺寸管理）
* 位运算（页号、对齐）

🔹 **操作系统与内存管理**

* 分页机制（8KB 页）
* 堆空间申请（VirtualAlloc / mmap）
* 内碎片 / 外碎片处理
* 页合并策略（前合并 + 后合并）

🔹 **高并发与锁**

* 线程本地存储（TLS）
* 细粒度锁（针对 CentralCache 分桶加锁）
* 粗粒度大锁（PageCache）
* 无锁路径（ThreadCache 内部）

🔹 **性能优化**

* 批量申请/回收减少系统调用
* 连续内存提升 Cache 友好性
* 基数树取代 unordered_map 加快查找
* 预分配策略降低动态开辟


## 📁 项目目录结构

```
ConcurrentMemoryPool/
│
├── 头文件/
│   ├── CentralCache.h        # CentralCache 的声明，负责共享对象池管理
│   ├── Common.h              # 通用宏、常量、类型定义（如 PAGE_SHIFT、MAX_BYTES）
│   ├── ConcurrentAlloc.h     # 对外暴露的统一接口：ConcurrentAlloc / ConcurrentFree
│   ├── ObjectPool.h          # 定长对象池，实现Span/ThreadCache等对象的无锁回收与复用
│   ├── PageCache.h           # PageCache 声明，负责页级 Span 的分配与回收、合并
│   ├── PageMap.h             # 单层基数树实现，用于页号 -> Span 的高速映射
│   ├── ThreadCache.h         # ThreadCache 声明，每线程的小对象缓存
│
├── 源文件/
│   ├── BenchMark.cpp         # 多线程压力测试、性能对比（malloc vs 内存池）
│   ├── CentralCache.cpp      # CentralCache 实现：批量分配/回收、Span 切分
│   ├── PageCache.cpp         # PageCache 实现：Span 管理、切分、合并、映射写入
│   ├── ThreadCache.cpp       # ThreadCache 实现：无锁分配、慢启动、回收逻辑
│   ├── UnitTest.cpp          # 单元测试，测试对齐、映射、Span 分配逻辑是否正确
│
└── README.md
```

## ⚙️ 使用方法

1️⃣ **分配内存**

```cpp
void* p = ConcurrentAlloc(64);
```

2️⃣ **释放内存（无需传 size）**

```cpp
ConcurrentFree(p);
```

3️⃣ **运行 Benchmark**

```cpp
BenchMark();
```

## ⚡ 性能对比

1️⃣ **固定大小（16B）**

```
malloc/free:             ~560 ms
ConcurrentMemoryPool:    ~220 ms（≈2.5×）
```

2️⃣ **随机大小（1B ~ 8KB）**

```
malloc/free:             ~800 ms
ConcurrentMemoryPool:    ~150–250 ms（≈3–6×）
```

基数树优化后，在 PageCache 映射访问时几乎“零成本”，性能进一步提升。


## 🧠 项目亮点总结

- 模拟真实内存分配器，而不只是数据结构练习
- 涉及 OS、并发、内存管理、大项目结构化设计
- 多层 Cache 架构体现对现代 allocator 的理解
- 能清晰展示 “如何让程序跑得更快” 的工程能力
- 性能测试完备，结果可量化
- 有完整文档，结构清晰


## 📜 作者与许可

- Author：Edison
- Language：C++
- License：MIT
- Repository：[GitHub Link](https://github.com/LuvKobe/Concurrent-Memory-Pool)


