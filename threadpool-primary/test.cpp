#include <iostream>
#include <chrono>
#include <thread>

#include "threadpool.h"


/*
有些场景，是希望获取线程执行任务的返回值的
举例：
1+....+30000的和
thread1 1+...+10000
thread2 10001+...+20000
....

main thread:给每一个线程分配计算的区间，并等待他们算完返回结果，合并最终的结果
*/

using ULong = unsigned long long;

class MyTask : public Task
{
public:
	MyTask(int begin, int end)
		:begin_(begin)
		,end_(end)
	{
		
	}
	//1.怎么设计run的返回值，可以表示任意的类型呢？
	//java python object是所有其他类类型的基类
	//c++17 Any类型
	Any run() //最终就在线程池分配的线程中去做事情
	{
		std::cout << "tid:" << std::this_thread::get_id() << "begin!" << std::endl;
		std::this_thread::sleep_for(std::chrono::seconds(3));
		ULong sum = 0;
		for (ULong i = begin_; i < end_; i++)
		{
			sum += i;
		}

		std::cout << "tid:" << std::this_thread::get_id() << "end!" << std::endl;
		return sum;
	}
private:
	int begin_;
	int end_;
};
int main()
{
	{
		ThreadPool pool;
		pool.setMode(PoolMode::MODE_CACHED);
		pool.start(4);

		//linux上，这些result也是局部对象 是需要析构的！！！
		Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 1000000));
		Result res2 = pool.submitTask(std::make_shared<MyTask>(1, 1000000));
		pool.submitTask(std::make_shared<MyTask>(1, 1000000));
		pool.submitTask(std::make_shared<MyTask>(1, 1000000));
		pool.submitTask(std::make_shared<MyTask>(1, 1000000));
		pool.submitTask(std::make_shared<MyTask>(1, 1000000));
		//ULong sum1 = res1.get().cast_<ULong>();
		//std::cout << sum1 << std::endl;
	}//Result要析构
	
	std::cout << "main over!" << std::endl;
	getchar();

#if 0
	{
		ThreadPool pool;
		//用户自己设置线程池的工作模式
		pool.setMode(PoolMode::MODE_CACHED);

		//启动线程池
		pool.start(4);

		//如何设计result机制
	
		Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 1000000));
		Result res2 = pool.submitTask(std::make_shared<MyTask>(1000001, 2000000));
		Result res3 = pool.submitTask(std::make_shared<MyTask>(2000001, 3000000));

		pool.submitTask(std::make_shared<MyTask>(2000001, 3000000));
		pool.submitTask(std::make_shared<MyTask>(2000001, 3000000));
		pool.submitTask(std::make_shared<MyTask>(2000001, 3000000));


		// 随着task被执行完，task对象也没了，依赖于task对象的result对象也没了
		ULong sum1 = res1.get().cast_<ULong>();//gget返回了一个any类型，怎么转回具体的类型
		ULong sum2 = res2.get().cast_<ULong>();
		ULong sum3 = res3.get().cast_<ULong>();

		std::cout << (sum1 + sum2 + sum3) << std::endl;

		/*pool.submitTask(std::make_shared<MyTask>());
		pool.submitTask(std::make_shared<MyTask>());
		pool.submitTask(std::make_shared<MyTask>());
		pool.submitTask(std::make_shared<MyTask>());*/

	}

	getchar();

#endif
}