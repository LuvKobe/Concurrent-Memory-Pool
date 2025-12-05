#define _CRT_SECURE_NO_WARNINGS 1

#include "CentralCache.h"
#include "PageCache.h"

// 类外初始化静态成员
CentralCache CentralCache::_sInst;

// 从SpanList或者page cache获取一个非空的span
Span* CentralCache::GetOneSpan(SpanList& list, size_t size)
{
	// 1. 查看当前的spanlist中是否还有未分配对象的span
	Span* it = list.Begin();
	while (it != list.End())
	{
		// 如果这个span下面的list不为空, 那么就说明有对象
		if (it->_freelist != nullptr)
		{
			return it;
		}
		else // 如果为空, 那么就去找下一个span
		{
			it = it->_next;
		}
	}

	// 在找page cache之前，先把central的桶锁给解除掉，
	// 这样如果其他线程释放回来，就不会阻塞
	list._mtx.unlock();

	// 2. 走到这里说明没有空闲的span了, 只能找page cache要
	PageCache::getInstance()->_pageMtx.lock();	// 给page cache整体加锁
	Span* span = PageCache::getInstance()->NewSpan(SizeClass::NumMovePage(size));
	span->_isUse = true;
	span->_objSize = size;
	PageCache::getInstance()->_pageMtx.unlock();// 给page cache整体解锁

	// 下面是对获取的span进行切分，不需要加锁，因为这会儿其他线程访问不到这个span

	//	2.1 通过页号，计算span页的大块内存的起始地址：页号 <<= page_shift (即 页号 * 每页的大小 * 1024)
	char* start = (char*)(span->_pageId << PAGE_SHIFT);

	//	2.2 计算span的大块内存的大小
	size_t bytes = span->_n << PAGE_SHIFT;
	char* end = start + bytes;

	// 3. 对span进行切分, 把大块内存切成自由链表链接起来
	//	3.1 先切一块儿下来去做头，方便尾插
	span->_freelist = start;
	start += size;
	void* tail = span->_freelist;
	// 尾插
	// 需要注意一下这里有一个bug，特殊情况下，span切完的最后一小块内存
	// 可能不够单个对象大小，分配出去使用会存在越界问题，所以把这个单个不够
	// 对象"丢弃"即可，不用担心内存泄漏，因为span的对象都使用完以后，page cache
	// 回收是按页回收的，这个丢弃的小内存又回去了。
	// while (start < end) 有bug的写法
	while (start + size < end)
	{
		NextObj(tail) = start;
		tail = NextObj(tail);
		start += size;
	}
	NextObj(tail) = nullptr;

	// 切好span以后，需要把span挂到桶里面去的时候，再加锁
	list._mtx.lock();

	// 3.2 把span插入到list里面去
	list.PushFront(span);

	return span;
}

// 从中心缓存获取一定数量的对象给thread cache
size_t CentralCache::FetchRangeObj(void*& start, void*& end, size_t batchNum, size_t size)
{
	// 1. 计算桶的位置(因为 thread cache 和 central cache 的哈希桶的一一对应的, 所以先算一下, 要的是哪个桶里面的)
	size_t index = SizeClass::Index(size);

	_spanLists[index]._mtx.lock(); // 先加锁

	// 从 span 中获取 batchNum 个对象
	// 如果不够 batchNum 个, 那么就有多少拿多少个
	// 先去 spanList 里面找一个非空的 Span, 如果没有找到, 那么就需要去 page cache 里面申请
	Span* span = GetOneSpan(_spanLists[index], size);
	assert(span);
	assert(span->_freelist);

	start = span->_freelist;
	end = start;
	size_t actualNum = 1;
	size_t i = 0;
	while (i < batchNum - 1 && NextObj(end) != nullptr)	// end->next != nullptr
	{
		end = NextObj(end);	// end = end->next
		++i;
		++actualNum;
	}
	span->_freelist = NextObj(end);
	NextObj(end) = nullptr;
	span->_usecount += actualNum; // 释放流程用的

	_spanLists[index]._mtx.unlock(); // 再解锁

	return actualNum;
}

// ThreadCache把它一个桶里面的一段挂的链表 还给span
void CentralCache::ReleaseListToSpans(void* start, size_t size)
{
	size_t index = SizeClass::Index(size);
	_spanLists[index]._mtx.lock();

	while (start)
	{
		void* next = NextObj(start);

		// 开始头插
		Span* span = PageCache::getInstance()->MapObjectToSpan(start);
		NextObj(start) = span->_freelist;
		span->_freelist = start;
		span->_usecount--;
		
		// 此时，说明span切分出去的所有小块儿内存都回来了
		if (0 == span->_usecount)	
		{
			// 1. 从桶下面取出完整的span
			_spanLists[index].Erase(span);
			span->_freelist = nullptr;
			span->_next = nullptr;
			span->_prev = nullptr;

			// 解除Central Cache的桶锁
			// 因为其他线程也有可能会在桶里面申请 / 释放内存
			_spanLists[index]._mtx.unlock();

			// 2. 这个span就可以再回收给page cache，然后page cache可以再尝试去做前后页的合并
			// 另外, 释放span给PageCache时，需要使用PageCache的整体锁
			PageCache::getInstance()->_pageMtx.lock();
			PageCache::getInstance()->ReleaseSpanToPageCache(span);
			PageCache::getInstance()->_pageMtx.unlock();

			// 添加Central Cache的桶锁
			_spanLists[index]._mtx.lock();
		}

		start = next;
	}
	_spanLists[index]._mtx.unlock();
}