#define _CRT_SECURE_NO_WARNINGS 1

// 对项目进行综合测试
#include"ConcurrentAlloc.h"

// ntimes 一轮申请和释放内存的次数
// rounds 轮次
// nworks 线程数量

// 测试 malloc / free
void BenchmarkMalloc(size_t ntimes, size_t nworks, size_t rounds)
{
	std::vector<std::thread> vthread(nworks);
	std::atomic<size_t> malloc_costtime = 0;
	std::atomic<size_t> free_costtime = 0;

	for (size_t k = 0; k < nworks; ++k)
	{
		vthread[k] = std::thread([&, k]() {
			std::vector<void*> v;
			v.reserve(ntimes);

			for (size_t j = 0; j < rounds; ++j)
			{
				size_t begin1 = clock();
				for (size_t i = 0; i < ntimes; i++)
				{
					//v.push_back(malloc(16));
					v.push_back(malloc((16 + i) % 8192 + 1));
				}
				size_t end1 = clock();

				size_t begin2 = clock();
				for (size_t i = 0; i < ntimes; i++)
				{
					free(v[i]);
				}
				size_t end2 = clock();
				v.clear();

				malloc_costtime += (end1 - begin1);
				free_costtime += (end2 - begin2);
			}
			});
	}

	for (auto& t : vthread)
	{
		t.join();
	}

	printf("%u 个线程并发执行 %u 轮次, 每轮 malloc 了 %u 次, 花费 %u ms\n",
		nworks, rounds, ntimes, malloc_costtime.load());

	printf("%u 个线程并发执行 %u 轮次, 每轮 free 了 %u 次, 花费 %u ms\n",
		nworks, rounds, ntimes, free_costtime.load());

	printf("%u 个线程并发 malloc && free 了 %u 次, 总计花费 %u ms\n",
		nworks, nworks * rounds * ntimes, malloc_costtime.load() + free_costtime.load());
}


// ntimes：单轮次申请释放次数 
// nworks：线程数 
// rounds：轮次

// 测试自己写的高并发内存池
void BenchmarkConcurrentMalloc(size_t ntimes, size_t nworks, size_t rounds)
{
	std::vector<std::thread> vthread(nworks);
	std::atomic<size_t> malloc_costtime = 0;
	std::atomic<size_t> free_costtime = 0;

	for (size_t k = 0; k < nworks; ++k)
	{
		vthread[k] = std::thread([&]() {
			std::vector<void*> v;
			v.reserve(ntimes);

			for (size_t j = 0; j < rounds; ++j)
			{
				size_t begin1 = clock();
				for (size_t i = 0; i < ntimes; i++)
				{
					//v.push_back(ConcurrentAlloc(16));
					v.push_back(ConcurrentAlloc((16 + i) % 8192 + 1));
				}
				size_t end1 = clock();

				size_t begin2 = clock();
				for (size_t i = 0; i < ntimes; i++)
				{
					ConcurrentFree(v[i]);
				}
				size_t end2 = clock();
				v.clear();

				malloc_costtime += (end1 - begin1);
				free_costtime += (end2 - begin2);
			}
			});
	}

	for (auto& t : vthread)
	{
		t.join();
	}

	printf("%u 个线程并发执行 %u 轮次, 每轮 concurrent Alloc 了 %u 次, 花费 %u ms\n",
		nworks, rounds, ntimes, malloc_costtime.load());

	printf("%u 个线程并发执行 %u 轮次, 每轮 Concurrent Dealloc 了 %u 次, 花费 %u ms\n",
		nworks, rounds, ntimes, free_costtime.load());

	printf("%u 个线程并发 Concurrent Alloc && Concurrent Dealloc 了 %u 次, 总计花费 %u ms\n",
		nworks, nworks * rounds * ntimes, malloc_costtime.load() + free_costtime.load());
}

int main()
{
	size_t n = 1000;
	cout << "==========================================================" << endl;
	BenchmarkConcurrentMalloc(n, 4, 10);
	cout << endl << endl;

	BenchmarkMalloc(n, 4, 10);
	cout << "==========================================================" << endl;

	return 0;
}