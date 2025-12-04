#define _CRT_SECURE_NO_WARNINGS 1

#include "ObjectPool.h"
#include "ConcurrentAlloc.h"
// 进行单元测试


void Alloc1()
{
	for (size_t i = 0; i < 5; ++i)
	{
		void* ptr = ConcurrentAlloc(6); // 一次申请 6 字节
	}
}

void Alloc2()
{
	for (size_t i = 0; i < 5; ++i)
	{
		void* ptr = ConcurrentAlloc(7); // 一次申请 6 字节
	}
}

void TLStest()
{
	// 创建了 t1 线程去执行 Alloc1 函数
	std::thread t1(Alloc1);
	t1.join();


	std::thread t2(Alloc2);
	t2.join();
}

void TestConcurrentAlloc1()
{
	// 申请
	void* p1 = ConcurrentAlloc(6);
	void* p2 = ConcurrentAlloc(8);
	void* p3 = ConcurrentAlloc(1);
	void* p4 = ConcurrentAlloc(7);
	void* p5 = ConcurrentAlloc(8);

	cout << p1 << endl;
	cout << p2 << endl;
	cout << p3 << endl;
	cout << p4 << endl;
	cout << p5 << endl;

	// 释放
	ConcurrentFree(p1);
	ConcurrentFree(p2);
	ConcurrentFree(p3);
	ConcurrentFree(p4);
	ConcurrentFree(p5);
}

void TestConcurrentAlloc2()
{
	for (size_t i = 0; i < 5; ++i)
	{
		void* p1 = ConcurrentAlloc(6);
		cout << p1 << endl;
	}
	void* p2 = ConcurrentAlloc(8);
	cout << p2 << endl;
}


// 验证地址转换
void TestAddressShift()
{
	PAGE_ID id1 = 2000;
	PAGE_ID id2 = 2001;
	
	// p1是第一页, 它的起始地址是2000
	// p2是第二页, 它的起始地址是2001
	char* p1 = (char*)(id1 << PAGE_SHIFT);
	char* p2 = (char*)(id2 << PAGE_SHIFT);

	//cout << p1 << endl;
	//cout << p2 << endl;

	// 一页可以切分成很多的小内存块儿
	// 那么，验证第一页中，所有的小内存块的地址都是2000
	while (p1 < p2)
	{
		cout << (void*)p1 << " : " << ((PAGE_ID)p1 >> PAGE_SHIFT) << endl;
		p1 += 8;
	}
/* 下面就是一页被切分出的小块儿内存，这些内存就是被挂到thread cache中的每个哈希桶下面的。
00FA0030 : 2000
00FA0038 : 2000
00FA0040 : 2000
00FA0048 : 2000
00FA0050 : 2000
00FA0058 : 2000
00FA0060 : 2000
00FA0068 : 2000
*/
}


// 测试多线程场景
void MultiThreadAlloc1()
{
	// 申请
	std::vector<void*> v;
	for (size_t i = 0; i < 5; ++i)
	{
		void* ptr = ConcurrentAlloc(6); // 一次申请 6 字节
		v.push_back(ptr);
	}

	// 释放
	for (auto e : v)
	{
		//ConcurrentFree(e, 6);
		ConcurrentFree(e);
	}
}

void MultiThreadAlloc2()
{
	// 申请
	std::vector<void*> v;
	for (size_t i = 0; i < 5; ++i)	// 创建 5 次
	{
		void* ptr = ConcurrentAlloc(6); // 一次申请 6 字节的内存对象
		v.push_back(ptr);
	}

	// 释放
	for (auto e : v)
	{
		//ConcurrentFree(e, 6);
		ConcurrentFree(e);

	}
}

void TestMultiThread()
{
	// 创建了 t1 线程去执行 Alloc1 函数
	std::thread t1(MultiThreadAlloc1);
	std::thread t2(MultiThreadAlloc2);

	t1.join();
	t2.join();
}

// 测试大内存
void BigAlloc()
{
	// 直接去申请257KB的内存, 因为1KB = 1024Byte
	void* p1 = ConcurrentAlloc(257 * 1024);
	//ConcurrentFree(p1, 257 * 1024);
	ConcurrentFree(p1);


	// 申请129页, 一页的大小为8k, 1KB = 1024Byte
	// 所以总共就是：129 * 8 * 1024 个Byte
	void* p2 = ConcurrentAlloc(129 * 8 * 1024);
	//ConcurrentFree(p2, 129 * 8 * 1024);
	ConcurrentFree(p2);

}



//int main()
//{
//	TestObjectPool();
//
//	//TLStest();
//
//	//TestConcurrentAlloc1();
//	//TestAddressShift();
//
//	//TestMultiThread();
//
//	BigAlloc();
//
//	return 0;
//}

