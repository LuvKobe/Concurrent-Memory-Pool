#pragma once

// 存放公共头文件
#include <iostream>
#include <vector>
#include <algorithm>
#include <unordered_map>

#include <thread>
#include <mutex>
#include <ctime>
#include <atomic>
#include <cassert>

#include "ObjectPool.h"

using std::cout;
using std::endl;

//#ifdef _WIN32 
//	#include <windows.h>
//#else
//	// linux下brk / mmap 的头文件
//#endif

// 
static const size_t MAX_BYTES = 256 * 1024;
static const size_t NFREELISTS = 208; // 哈希桶的总数量
static const size_t NPAGES = 129;
static const size_t PAGE_SHIFT = 13; // 页大小转换偏移, 即一页定义为2^13,也就是8KB

// 32 位平台下: 2^(32-13)=2¹⁹页
#ifdef _WIN32 
	typedef size_t PAGE_ID; 
#elif _WIN64 
	typedef unsigned long long PAGE_ID;
#endif

// 获取内存对象中存储的头4 or 8字节值，即链接的下一个对象的地址
static void*& NextObj(void* obj)
{
	return *(void**)obj;
}

// 管理小对象的自由链表
class FreeList
{
public:
	// 插入(来了一个对象的时候，需要插入到自由链表中)
	void Push(void* obj)
	{
		// obj不能为空
		assert(obj);

		// 头插
		// 把这个对象的前4字节（64位平台下就是8字节）作为【next】指针
		//*((void**)obj) = _freeList;
		NextObj(obj) = _freeList;
		_freeList = obj;

		++_size;
	}

	// 从链表头部取出一个对象，然后返回地址，并从链表头部移除
	void* Pop()
	{
		// 链表不能为空
		assert(_freeList);

		// 头删
		void* obj = _freeList;
		_freeList = NextObj(obj);
		--_size;

		return obj;
	}

	// 从给定范围的链表中插入
	void PushRange(void* start, void* end, size_t n)
	{
		NextObj(end) = _freeList;
		_freeList = start;

		_size += n;
	}

	void PopRange(void*& start, void*& end, size_t n)
	{
		assert(n >= _size);
		start = _freeList;
		end = start;

		for (size_t i = 0; i < n - 1; ++i)
		{
			end = NextObj(end);
		}
		
		_freeList = NextObj(end);
		NextObj(end) = nullptr;
		_size -= n;
	}

	// 判断链表是否为空
	bool Empty()
	{
		return _freeList == nullptr;
	}

	//
	size_t& MaxSize()
	{
		return _maxSize;
	}

	size_t Size()
	{
		return _size;
	}

private:
	void* _freeList = nullptr;
	size_t _maxSize = 1;
	size_t _size = 0;	// 记录个数
};


// 计算对象大小的对齐映射规则
class SizeClass
{
public:

	// 第二种写法
	static inline size_t _RoundUp(size_t bytes, size_t alignNum)
	{
		return ((bytes + alignNum - 1) & ~(alignNum - 1));
	}

	// 对齐大小计算
	static inline size_t RoundUp(size_t size)
	{
		if (size <= 128)
		{
			return _RoundUp(size, 8);
		}
		else if (size <= 1024)
		{
			return _RoundUp(size, 16);
		}
		else if (size <= 8 * 1024)
		{
			return _RoundUp(size, 128);
		}
		else if (size <= 64 * 1024)
		{
			return _RoundUp(size, 1024);
		}
		else if (size <= 256 * 1024)
		{
			return _RoundUp(size, 8*1024);
		}
		else  // 如果申请的内存大于256KB, 那么就以一页为单位进行对齐
		{
			//assert(false);
			//return -1;
			return _RoundUp(size, 1 << PAGE_SHIFT);
		}
	}
	
	// 第二种写法
	// align_shift是什么呢？
	// 如果 对齐数alignNum = 8，那么 align_shift = 3，因为 2^3 = 8
	// 同时 1 << 3 ---> 1 * 2 * 2 * 2 = 1 * 2^3 = 8
	//		1 >> 3 ---> 1 / 2^3 = 1 / 8 = 0
	static inline size_t _Index(size_t bytes, size_t align_shift)
	{
		return ((bytes + (1 << align_shift) - 1) >> align_shift) - 1;
	}

	// 计算映射的哪一个自由链表桶
	static inline size_t Index(size_t bytes)
	{
		assert(bytes <= MAX_BYTES);

		// 每个区间有多少个链
		static int group_array[4] = { 16, 56, 56, 56 };
		if (bytes <= 128) 
		{
			return _Index(bytes, 3);
		}
		else if (bytes <= 1024) 
		{
			return _Index(bytes - 128, 4) + group_array[0];
		}
		else if (bytes <= 8 * 1024) 
		{
			return _Index(bytes - 1024, 7) + group_array[1] + group_array[0];
		}
		else if (bytes <= 64 * 1024) 
		{
			return _Index(bytes - 8 * 1024, 10) + group_array[2] + group_array[1]
				+ group_array[0];
		}
		else if (bytes <= 256 * 1024) 
		{
			return _Index(bytes - 64 * 1024, 13) + group_array[3] +
				group_array[2] + group_array[1] + group_array[0];
		}
		else 
		{
			assert(false);
		}
		return -1;
	}

	// 一次 thread cache 从中心缓存获取多少个(对象)
	static size_t NumMoveSize(size_t size)
	{
		assert(size > 0);

		// [2, 512]，一次批量移动多少个对象的(慢启动)上限值
		// 小对象一次批量上限高
		// 小对象一次批量上限低
		int num = MAX_BYTES / size;
		if (num < 2)
			num = 2;
		if (num > 512)
			num = 512;
		return num;
	}

	// 计算一次向系统获取几个页
	// 单个对象 8byte
	// ...
	// 单个对象 256KB
	static size_t NumMovePage(size_t size)
	{
		size_t num = NumMoveSize(size);
		size_t npage = num * size;

		npage >>= PAGE_SHIFT;
		if (npage == 0)
			npage = 1;
		return npage;
	}
};


// Span管理一个跨度的大块内存, 管理以页为单位的大块内存
struct Span
{
	PAGE_ID _pageId = 0;	// 页号
	size_t _n = 0;			// 页的数量

	// 双向循环链表
	Span* _next = nullptr;
	Span* _prev = nullptr;

	void* _freelist = nullptr;  // 大块内存切小链接起来，这样回收回来的内存也方便链接
	size_t _usecount = 0;   // 使用计数，==0 说明所有对象都回来了
	
	bool _isUse = false;	// 是否在使用
	size_t _objSize = 0;	// 切出来的单个对象的大小
};

// 带头双向循环链表
class SpanList
{
public:
	SpanList()
	{
		// _head = new Span;
		// 修正一bug，之前这里直接使用new，我们目标是要替代new/malloc
		// 所以这里也要替换掉，使用对象池
		static ObjectPool<Span> spanPool;
		_head = spanPool.New();

		_head->_next = _head;
		_head->_prev = _head;
	}

	// 
	Span* Begin()
	{
		return _head->_next;
	}

	// 
	Span* End()
	{
		return _head;
	}

	// 头插
	void PushFront(Span* span)
	{
		Insert(Begin(), span);
	}

	//
	Span* PopFront()
	{
		Span* span = Begin();
		Erase(span);
		return span;
	}

	// 判空
	bool Empty()
	{
		return _head->_next == _head;
	}

	// 在 pos 位置插入
	void Insert(Span* pos, Span* newSpan)
	{
		assert(pos);
		assert(newSpan);

		// [prev] ----- [pos]
		// 在 pos 前面插入
		// [prev] -- [newSpan] -- [pos]
		Span* prev = pos->_prev;
		prev->_next = newSpan;
		newSpan->_prev = prev;
		newSpan->_next = pos;
		pos->_prev = newSpan;
	}

	// 在 pos 位置删除(这里不是真的删除, 只是解除链接)
	void Erase(Span* pos)
	{
		assert(pos);
		assert(pos != _head);

		// 删掉 [pos]
		// [prev] -- [pos] -- [next]
		// [prev] -- [next]
		Span* prev = pos->_prev;
		Span* next = pos->_next;

		prev->_next = next;
		next->_prev = prev;

		// 注意: 我们不需要 delete[pos]
		// 因为这个 [pos] 是需要还给下一层的 page cache 的
	}

private:
	Span* _head = nullptr;	// 头结点

public:
	// 如果两个线程访问同一个桶, 那么就会存在竞争, 故而需要加锁
	// 加了锁以后, A 线程在获取资源的同时, B 线程只能阻塞等待
	std::mutex _mtx;	// 桶锁
};