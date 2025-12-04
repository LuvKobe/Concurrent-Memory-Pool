#pragma once

#include "Common.h"
#include "ObjectPool.h"

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

	std::unordered_map<PAGE_ID, Span*> _idSpanMap;	// 建立【页号 -- Span】之间的映射

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