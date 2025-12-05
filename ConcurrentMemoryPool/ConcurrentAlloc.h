#pragma once

#include "ThreadCache.h"
#include "PageCache.h"
#include "ObjectPool.h"

// 申请
static void* ConcurrentAlloc(size_t size)
{
	if (size > MAX_BYTES)	// 如果申请的内存大于256KB
	{
		size_t alignSize = SizeClass::RoundUp(size);
		size_t kpage = alignSize >> PAGE_SHIFT;

		//cout << "alignSize(对齐大小): " << alignSize << ", " << "kpage(申请的页大小): " << kpage << endl;
		
		PageCache::getInstance()->_pageMtx.lock();
		Span* span = PageCache::getInstance()->NewSpan(kpage); // 去page cache里面要一个K页的span的页号转换出来的地址
		span->_objSize = size;
		PageCache::getInstance()->_pageMtx.unlock();

		void* ptr = (void*)(span->_pageId << PAGE_SHIFT);

		return ptr;
	}
	else
	{
	// 通过 TLS 每个线程可以无锁的获取自己专属的 ThreadCache 对象
	// 如果 ThreadCache 对应的 size 映射的 哈希桶 里面有对象，那么直接 Pop() 一下，效率非常高
	// 此时，你有多个线程并行的走，并且是无锁的。
	// 因为锁的竞争是非常激烈的，并且是会有很多消耗的，比如互斥锁之类的，A运行的时候，B就不能运行，B要阻塞等待
		if (pTLSthreadcache == nullptr)
		{
			//pTLSthreadcache = new ThreadCache;

			// 也是使用定长内存池进行替换
			static ObjectPool<ThreadCache> tcPool;
			pTLSthreadcache = tcPool.New();
		}

		//cout << std::this_thread::get_id() << ":" << pTLSthreadcache << "申请对象成功" << endl;

		return pTLSthreadcache->Allocate(size);
	}	
}

// 释放
/*
static void ConcurrentFree(void* ptr, size_t size)
{
	if (size > MAX_BYTES)
	{
		Span* span = PageCache::getInstance()->MapObjectToSpan(ptr);
		
		PageCache::getInstance()->_pageMtx.lock();
		PageCache::getInstance()->ReleaseSpanToPageCache(span);
		PageCache::getInstance()->_pageMtx.unlock();
	}
	else
	{
		assert(pTLSthreadcache);
		cout << std::this_thread::get_id() << ":" << pTLSthreadcache << "释放对象成功" << endl;
		// 还需要给出 size，如果不给的话，我不知道你要还给哪个位置下的哈希桶
		pTLSthreadcache->Deallocate(ptr, size);
	}
}*/


static void ConcurrentFree(void* ptr)
{
	Span* span = PageCache::getInstance()->MapObjectToSpan(ptr);
	size_t size = span->_objSize;

	if (size > MAX_BYTES)
	{

		PageCache::getInstance()->_pageMtx.lock();
		PageCache::getInstance()->ReleaseSpanToPageCache(span);
		PageCache::getInstance()->_pageMtx.unlock();
	}
	else
	{
		assert(pTLSthreadcache);
		//cout << std::this_thread::get_id() << ":" << pTLSthreadcache << "释放对象成功" << endl;
		// 还需要给出 size，如果不给的话，我不知道你要还给哪个位置下的哈希桶
		pTLSthreadcache->Deallocate(ptr, size);
	}
}