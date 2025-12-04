#pragma once

// 声明

#include "Common.h"

// thread cache本质是由一个哈希映射的对象自由链表构成
class ThreadCache
{
public:
	// 申请和释放内存对象
	void* Allocate(size_t size);
	void Deallocate(void* ptr, size_t size);

	// 从中心缓存获取对象
	void* FetchFromCentralCache(size_t index, size_t size);

	// 释放对象时，链表过长时，回收内存回到中心缓存
	void ListTooLong(FreeList& list, size_t size);
private:
	// 用数组来模拟哈希表，每个数组的位置都挂了一个【_freeList】
	FreeList _freeLists[NFREELISTS];
};

// TLS thread local storage
// 假设你程序启动以后有 3 个线程，那么这 3 个线程各自都会有一个 tls_threadcache
// 这个变量在它所在的线程内是全局可访问的，但是不能被其他线程访问到，这样就保持了数据的线程独立性。
static _declspec(thread) ThreadCache* pTLSthreadcache = nullptr;

// 注意：给全局变量加上 static 保证了只在当前文件可见。

