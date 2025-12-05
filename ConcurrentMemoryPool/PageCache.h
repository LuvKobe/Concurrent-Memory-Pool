#pragma once

#include "Common.h"
#include "ObjectPool.h"
#include "PageMap.h"

// 1. page cache是一个以页为单位的span自由链表
// 2. 为了保证全局只有唯一的page cache，这个类被设计成了单例模式。
class PageCache
{
public:
	// 3. 公共静态成员函数：全局唯一获取实例的入口
	static PageCache* getInstance()
	{
		return &_sInst;
	}

public:
	// 向系统申请k页span(内存)挂到自由链表
	Span* NewSpan(size_t k);

	// 获取从对象到span的映射
	Span* MapObjectToSpan(void* obj);

	// 释放空闲span回到Pagecache，并合并相邻的span
	void ReleaseSpanToPageCache(Span* span);
private:
	SpanList _spanLists[NPAGES];	// 按页数映射

	// 我有内存块的地址, 那么就可以计算出当前内存块所在的页号
	// 并且现在有一个map存了【页号 -- Span】之间的映射
	// 那么我就可以通过页号，找到这个Span指针
	// 然后就可以把内存块挂到对应的span里面去了
	//std::unordered_map<PAGE_ID, Span*> _idSpanMap;	// 建立【页号 -- Span】之间的映射

	//std::unordered_map<PAGE_ID, size_t> _idSizeMap;	// 建立【页号 -- size】之间的映射 

	// 使用tcmalloc源码中实现基数树进行优化
	TCMalloc_PageMap1<32 - PAGE_SHIFT> _idSpanMap;

	// 使用定长内存池配合脱离使用new
	ObjectPool<Span> _spanPool;
private:
	// 1. 私有构造函数：禁止外部通过 new/栈实例创建
	PageCache()
	{}

	// 2. 禁止拷贝构造和赋值运算符（避免复制出多个实例）
	PageCache(const PageCache&) = delete;				// 禁用拷贝构造
	PageCache operator=(const PageCache&) = delete;	// 禁用赋值

	static PageCache _sInst;		// 单例(饿汉模式)

public:
	std::mutex _pageMtx;			// 加锁, 定义为公有的
};