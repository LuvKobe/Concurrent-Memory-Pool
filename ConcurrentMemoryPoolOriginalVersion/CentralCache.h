#pragma once

#include "Common.h"

// 单例模式（饿汉式）
class CentralCache
{
public:
	// 3. 公共静态成员函数：全局唯一获取实例的入口
	static CentralCache* getInstance()
	{
		return &_sInst;
	}

	// 从中心缓存获取一定数量的对象给thread cache
	size_t FetchRangeObj(void*& start, void*& end, size_t batchNum, size_t size);

	// 从SpanList或者page cache获取一个非空的span
	Span* GetOneSpan(SpanList& list, size_t size);

	// 将一定数量的对象释放到span跨度
	void ReleaseListToSpans(void* start, size_t size);

private:
	SpanList _spanLists[NFREELISTS];	// 按对齐方式映射

private:
	// 1. 私有构造函数：禁止外部通过 new/栈实例创建
	CentralCache()
	{}

	// 2. 禁止拷贝构造和赋值运算符（避免复制出多个实例）
	CentralCache(const CentralCache&) = delete;				// 禁用拷贝构造
	CentralCache operator=(const CentralCache&) = delete;	// 禁用赋值

	static CentralCache _sInst;	// 类加载时就初始化（程序启动时）
};