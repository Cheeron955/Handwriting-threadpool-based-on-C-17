// ThreadPool.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <iostream>
#include <functional>
#include <thread>
#include <future>
#include <chrono>

#include "threadpool.h"

using namespace std;


/*
* int sum1(int a,int b) return a+b;
如何能让线程池提交任务更加方便
1. pool.submitTask(sum1,10,20);
   pool.submitTask(sum2,1,2,3); 把函数当成任务
   submitTask：可变参模板编程
2. 我们自己造了一个Result以及相关类型，
	C++11 线程库 thread
	packaged_task(function函数对象) async

	packaged_task<int(int,int)> task(sum1); //左值拷贝被delete
	future<int> res=task.get_future();
	task(10,20);

	thread t(std::move(task),10,20);
	t.detach();//分离线程 

	cout<<res.get()<<endl;
	使用future来代替result节省线程池代码 并把其放入一个线程中去执行
*/

int sum1(int a, int b)
{
	std::this_thread::sleep_for(std::chrono::seconds(5));
	return a + b;
}

int sum2(int a, int b,int c)
{
	std::this_thread::sleep_for(std::chrono::seconds(5));
	return a + b + c;
}

int main()
{
	ThreadPool pool;

	//pool.setMode(PoolMode::MODE_CACHED);

	pool.start(2);

	future<int> res1 = pool.submitTask(sum1, 1, 2);
	future<int> res2 = pool.submitTask(sum2, 1, 2,3);
	future<int> res3 = pool.submitTask([](int d, int e)->int {
		int sum = 0;
		for (int i = d; i < e; i++)
			sum += i;
		return sum;
		},1, 100);

	future<int> res4 = pool.submitTask([](int d, int e)->int {
		int sum = 0;
		for (int i = d; i < e; i++)
			sum += i;
		return sum;
		}, 1, 100);

	future<int> res5 = pool.submitTask([](int d, int e)->int {
		int sum = 0;
		for (int i = d; i < e; i++)
			sum += i;
		return sum;
		}, 1, 100);

	cout << res1.get() << endl;
	cout << res2.get() << endl;
	cout << res3.get() << endl;
	cout << res4.get() << endl;
	cout << res5.get() << endl;
}


