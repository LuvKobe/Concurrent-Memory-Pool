#define _CRT_SECURE_NO_WARNINGS 1

#include "PageCache.h"

// 类外初始化静态成员
PageCache PageCache::_sInst;

// 获取一个 K 页的 span
Span* PageCache::NewSpan(size_t k)
{
	assert(k > 0);

	// 如果K的页数大于128页，那么就去找堆申请
	if (k > NPAGES - 1)
	{
		//cout << "申请的page大于128页, 开始向堆申请" << endl;
		void* ptr = SystemAlloc(k);

		//Span* span = new Span;
		Span* span = _spanPool.New(); // 替换

		span->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT;
		span->_n = k;

		//_idSpanMap[span->_pageId] = span;
		_idSpanMap.set(span->_pageId, span);	// 使用基数树优化

		return span;
	}

	// 先检查第k个桶里面有没有span
	if (!_spanLists[k].Empty())
	{
		Span* kSpan = _spanLists[k].PopFront();

		// 建立【页号 -- span】的映射，方便central cache回收小块儿内存时，查找对应的span
		for (PAGE_ID i = 0; i < kSpan->_n; ++i)
		{
			//_idSpanMap[kSpan->_pageId + i] = kSpan;
			_idSpanMap.set(kSpan->_pageId + i, kSpan);	// 使用基数树优化
		}

		return kSpan;
	}
	
	// 走到这里, 说明第k个桶里面是空的;
	// 那么就去检查一下后面的桶里面有没有span, 如果有, 就可以把它进行切分
	for (size_t i = k + 1; i < NPAGES; i++)
	{
		if (!_spanLists[i].Empty())
		{
			// 切分成一个k页的span，和一个n-k页的span
			// 然后把k页的span返回给central cache
			// 最后把n-k页的span挂到第n-k个桶中去
			Span* nSpan = _spanLists[i].PopFront();
			//Span* kSpan = new Span;
			Span* kSpan = _spanPool.New();

			// 在nSpan的头部切一个k页下来
			kSpan->_pageId = nSpan->_pageId;	// 页号
			kSpan->_n = k;	// 页数

			nSpan->_pageId += k;
			nSpan->_n -= k;	//	还剩下n-k页

			_spanLists[nSpan->_n].PushFront(nSpan);	// 把剩下的n-k页挂到第n-k个位置

			// 存储(n-k)的Span的首尾页号跟(n-k)的Span的映射，
			// 方便page cache回收内存时，进行的合并查找

			// 假设第一页的pageId是1000，总共有5页
			// 那么最后一页就是 1000 + 5 - 1 = 1004
			//_idSpanMap[nSpan->_pageId] = nSpan;	// 第一页
			//_idSpanMap[nSpan->_pageId + nSpan->_n - 1] = nSpan;	// 最后一页
			_idSpanMap.set(nSpan->_pageId, nSpan);	// 使用基数树优化
			_idSpanMap.set(nSpan->_pageId + nSpan->_n - 1, nSpan);	// 使用基数树优化

			// 建立【页号 -- span】的映射，方便central cache回收小块儿内存时，查找对应的span
			for (PAGE_ID i = 0; i < kSpan->_n; ++i)
			{
				//_idSpanMap[kSpan->_pageId + i] = kSpan;
				_idSpanMap.set(kSpan->_pageId + i, kSpan);	// 使用基数树优化
			}

			return kSpan;
		}
	}

	// 走到这个位置, 说明后面没有大页的span
	// 此时, 需要去找堆要一个128页的span
	//Span* bigSpan = new Span;
	Span* bigSpan = _spanPool.New();

	void* ptr = SystemAlloc(NPAGES - 1); // 根据 kpage（页数量）向 操作系统申请一大片连续虚拟内存。
	// 通常 1 页 = 8KB = 2¹³ Byte, 1KB = 1024Byte
	// 那么第0页的起始地址为0
	// 第一页的起始地址为 8*1024 = 1 * 8k
	// 第二页的起始地址为 16*1024 = 2 * 8k
	// 那么现在已经知道了地址，如何计算页号呢？
	// 很简单：地址 / 8k = 页号
	bigSpan->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT; // 页号
	bigSpan->_n = NPAGES - 1;	// 页的数量
	
	_spanLists[bigSpan->_n].PushFront(bigSpan);

	//cout << "申请的对象大于256KB, 那么向PageCache直接申请整页" << endl;
	return NewSpan(k);
}


// 获取从对象到span的映射
Span* PageCache::MapObjectToSpan(void* obj)
{
	PAGE_ID id = ((PAGE_ID)obj >> PAGE_SHIFT);	// 计算页号

	/*
	std::unique_lock<std::mutex> lock(_pageMtx); // 添加RAII风格的锁
	auto ret = _idSpanMap.find(id);
	if (ret != _idSpanMap.end())
	{
		return ret->second;		// 找到了，就返回页号所对应的span指针
	}
	else  // 不可能找不到！
	{
		assert(false);	
		return nullptr;
	}*/

	// 使用基数树进行优化
	auto ret = (Span*)_idSpanMap.get(id);
	assert(ret != nullptr);
	return ret;
}

// 释放空闲span回到Pagecache，并合并相邻的span
void PageCache::ReleaseSpanToPageCache(Span* span)
{
	// 如果span的页数大于128页, 说明是找堆申请的, 直接还给堆
	if (span->_n > NPAGES - 1)
	{
		void* ptr = (void*)(span->_pageId << PAGE_SHIFT);
		SystemFree(ptr);
		//delete span;
		_spanPool.Delete(span);

		//cout << "把从堆上申请的【大于128页的span】还给堆" << endl;
		return;
	}

	// 对span前后的页, 尝试进行合并, 缓解内存碎片问题（解决外碎片）
	while (1)
	{
		PAGE_ID prevId = span->_pageId - 1;
		/*
		auto ret = _idSpanMap.find(prevId);
		// 前面的页号没有，不进行合并
		if (ret == _idSpanMap.end())	
		{
			break;
		}*/
		// 使用基数树进行优化
		auto ret = (Span*)_idSpanMap.get(prevId);
		if (ret == nullptr) {
			break;
		}

		// 前面相邻页的span在使用，不合并了
		//Span* prevSpan = ret->second;
		Span* prevSpan = ret;
		if (true == prevSpan->_isUse)
		{
			break;
		}

		// 此时合并出超过128页的span没办法管理，不合并了
		if (prevSpan->_n + span->_n > NPAGES - 1)
		{
			break;
		}

		// 除开上述三种情况以后，开始合并
		span->_pageId = prevSpan->_pageId;
		span->_n += prevSpan->_n;

		_spanLists[prevSpan->_n].Erase(prevSpan);
		//delete prevSpan;
		_spanPool.Delete(prevSpan);

	}

	// 向后合并
	while (1)
	{
		PAGE_ID nextId = span->_pageId + span->_n;	// 后面页的起始页号
		/*
		auto ret = _idSpanMap.find(nextId);
		// 后面的页号没有，不进行合并
		if (ret == _idSpanMap.end())
		{
			break;
		}*/
		// 使用基数树进行优化
		auto ret = (Span*)_idSpanMap.get(nextId);
		if (ret == nullptr) {
			break;
		}

		// 后面相邻页的span在使用，不合并了
		//Span* nextSpan = ret->second;
		Span* nextSpan = ret;
		if (true == nextSpan->_isUse)
		{
			break;
		}

		// 此时合并出超过128页的span没办法管理，不合并了
		if (nextSpan->_n + span->_n > NPAGES - 1)
		{
			break;
		}

		// 除开上述三种情况以后，开始合并
		span->_n += nextSpan->_n;

		_spanLists[nextSpan->_n].Erase(nextSpan);
		//delete nextSpan;
		_spanPool.Delete(nextSpan);
	}

	// 合并好以后，挂到对应的位置，并且要在map中建立首尾页的映射
	_spanLists[span->_n].PushFront(span);
	span->_isUse = false;
	
	//_idSpanMap[span->_pageId] = span;
	//_idSpanMap[span->_pageId + span->_n - 1] = span;
	_idSpanMap.set(span->_pageId, span);	// 使用基数树进行优化 
	_idSpanMap.set(span->_pageId + span->_n - 1, span);	// 使用基数树进行优化
}