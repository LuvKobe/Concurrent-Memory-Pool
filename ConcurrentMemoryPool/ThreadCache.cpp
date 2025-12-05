#define _CRT_SECURE_NO_WARNINGS 1

#include "ThreadCache.h"
#include "CentralCache.h"

void* ThreadCache::FetchFromCentralCache(size_t index, size_t size)
{
	// 慢开始反馈调节算法
	// 1. 最开始不会一次性向 central cache 一次批量要太多, 因为要太多了可能要不完
	// 2. 如果你不断有 size 大小的内存需求, 那么 batchNum 就会不断增长, 直到上限
	// 3. size 越大, 一次性向 central cache 要的 batchNum 就越小
	// 4. size 越小, 一次性向 central cache 要的 batchNum 就越大(慢慢增长变大)
	size_t batchNum = min(_freeLists[index].MaxSize(), SizeClass::NumMoveSize(size));
	// 那么我批量找你多要一些的好处就是：
	// 再下次我来了以后申请内存的时候, 就直接在 thread cache 申请就行, 就不需要找你 central cache了
	// 尽量能在 thread cache 里面申请最好
	if (batchNum == _freeLists[index].MaxSize())
	{
		_freeLists[index].MaxSize() += 1;
	}

	// 向 central cache 申请内存, 申请 batchNum 个 size 大小的对象
	void* start = nullptr;
	void* end = nullptr;
	size_t actualNum = CentralCache::getInstance()->FetchRangeObj(start, end, batchNum, size);
	assert(actualNum > 0);

	if (1 == actualNum)		// 如果只获取到了 1 个
	{
		assert(start == end);
		return start;
	}
	else	
	{
		// 如果从 central cache 获取到了多个对象, 那么就把开头的第 1 个对象返回给 [外面调用的线程]
		// 把剩下的对象，头插到 thread cache 的自由链表中
		_freeLists[index].PushRange(NextObj(start), end, actualNum - 1);
		return start;
	}
}

// 申请内存对象
void* ThreadCache::Allocate(size_t size)
{
	// size 应该是 <= 258kb 的
	assert(size <= MAX_BYTES);

	// 如何找到对应的桶呢？
	// 比如 size = 7，应该是取申请 8 字节，那么如何找到 8字节 对应的桶呢？
	size_t alignSize = SizeClass::RoundUp(size);
	size_t index = SizeClass::Index(size); 
	if (!_freeLists[index].Empty()) // 如果不为空, 那么说明可以去桶的下面取内存
	{
		return _freeLists[index].Pop();
	}
	else // 如果【桶】下面没有自由链表，那么就要去【中心缓存】中去获取
	{
		return FetchFromCentralCache(index, alignSize);
	}
}


// 释放内存对象
void ThreadCache::Deallocate(void* ptr, size_t size)
{
	assert(size <= MAX_BYTES);
	assert(ptr);

	// 计算你当前 size 在哪个桶里面（桶 = 数组，即 size 被映射到了数组的哪个位置）
	// 找出映射的自由链表桶，然后把对象插入进去
	size_t index = SizeClass::Index(size);
	_freeLists[index].Push(ptr);

	// 当链表长度大于一次批量申请的内存时, 就开始还一段list给central cache
	if (_freeLists[index].Size() >= _freeLists[index].MaxSize())
	{
		ListTooLong(_freeLists[index], size);
	}
}

// 释放对象时，链表过长时，回收内存回到中心缓存
void ThreadCache::ListTooLong(FreeList& list, size_t size)
{
	void* start = nullptr;
	void* end = nullptr;
	list.PopRange(start, end, list.MaxSize());	// 取一次批量的内存出来

	CentralCache::getInstance()->ReleaseListToSpans(start, size);
}