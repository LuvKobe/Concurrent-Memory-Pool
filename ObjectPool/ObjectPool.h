#pragma once

#pragma once

#include <iostream>
#include <mutex>
#include <vector>

using namespace std;

#ifdef _WIN32 
#include <windows.h>
#else
// linux下brk / mmap 的头文件
#endif

// 直接去堆上按页申请空间
inline static void* SystemAlloc(size_t kpage)
{
#ifdef _WIN32
	void* ptr = VirtualAlloc(0, kpage << 13, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
	// linux下brk mmap等
#endif
	if (ptr == nullptr)
		throw std::bad_alloc();
	return ptr;
}

// 释放从堆上申请的空间
inline static void SystemFree(void* ptr)
{
#ifdef _WIN32
	VirtualFree(ptr, 0, MEM_RELEASE);
#else
	// sbrk unmmap等
#endif
}

// 定长内存池
template<class T>
class ObjectPool
{
public:
	T* New()
	{
		T* obj = nullptr;

		{
			// 这里要修复一个bug，这里是需要加锁的，否则多线程使用同一个对象池对象，存在bug
			std::unique_lock<std::mutex> lock(_mtx);

			// 如果自由链表有对象，直接取一个
			if (_freeList != nullptr)
			{
				void* next = *((void**)_freeList); // 取第一个结点
				obj = (T*)_freeList;
				_freeList = next;
			}
			else  // 如果链表为空, 那么就去申请
			{
				// 剩余内存不够一个对象大小时, 则重新开大块空间
				if (_remainBytes < sizeof(T))
				{
					_remainBytes = 128 * 1024;
					//_memory = (char*)malloc(_remainBytes);
					_memory = (char*)SystemAlloc(_remainBytes >> 13);
					if (_memory == nullptr)
					{
						//printf("malloc error\n");
						//exit(-1);
						throw std::bad_alloc();
					}
				}

				// 剩余内存够一个对象大小时
				obj = (T*)_memory;
				size_t objSize = sizeof(T) < sizeof(void*) ? sizeof(void*) : sizeof(T);
				_memory += objSize;
				_remainBytes -= objSize;
			}
		}

		// 使⽤定位 new 调⽤ T 的构造函数初始化
		new(obj)T;
		return obj;
	}

	void Delete(T* obj)
	{
		std::unique_lock<std::mutex> lock(_mtx);

		//  显⽰调⽤的 T 的析构函数进⾏清理  
		obj->~T();

		// 头插，_freeList 最开始指向 NULL 

		// void* 在32位平台下是4字节，64位平台下是8字节
		*((void**)obj) = _freeList;

		_freeList = obj;
	}
private:
	char* _memory = nullptr;	//  指向内存块的指针
	int _remainBytes = 0;	//  内存块中剩余字节数  
	void* _freeList = nullptr;	//  管理还回来的内存对象的⾃由链表

	std::mutex _mtx;
};


// 二叉树结点
struct TreeNode
{
	int _val;
	TreeNode* _left;
	TreeNode* _right;

	TreeNode()
		: _val(0), _left(nullptr), _right(nullptr)
	{}
};

void TestObjectPool()
{
	// 申请释放的轮次
	const size_t Rounds = 3;

	// 每轮申请释放多少次
	const size_t N = 100000;

	std::vector<TreeNode*> v1;
	v1.reserve(N);

	// 调用系统的
	size_t begin1 = clock();
	for (size_t i = 0; i < Rounds; ++i)
	{
		for (int j = 0; j < N; ++j)
		{
			v1.push_back(new TreeNode);
		}
		for (int j = 0; j < N; ++j)
		{
			delete v1[j];
		}
		v1.clear();
	}
	size_t end1 = clock();

	ObjectPool<TreeNode> TNPool;
	std::vector<TreeNode*> v2;
	v2.reserve(N);

	size_t begin2 = clock();
	for (int i = 0; i < Rounds; ++i)
	{
		for (int j = 0; j < N; ++j)
		{
			v2.push_back(TNPool.New());
		}
		for (int j = 0; j < N; ++j)
		{
			TNPool.Delete(v2[j]);
		}
		v2.clear();
	}
	size_t end2 = clock();

	// 测试速度
	cout << "new cost time:" << end1 - begin1 << endl;
	cout << "object pool cost time:" << end2 - begin2 << endl;
}

