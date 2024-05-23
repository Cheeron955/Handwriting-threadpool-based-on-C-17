#include <iostream>
#include <chrono>
#include <thread>

#include "threadpool.h"


/*
��Щ��������ϣ����ȡ�߳�ִ������ķ���ֵ��
������
1+....+30000�ĺ�
thread1 1+...+10000
thread2 10001+...+20000
....

main thread:��ÿһ���̷߳����������䣬���ȴ��������귵�ؽ�����ϲ����յĽ��
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
	//1.��ô���run�ķ���ֵ�����Ա�ʾ����������أ�
	//java python object���������������͵Ļ���
	//c++17 Any����
	Any run() //���վ����̳߳ط�����߳���ȥ������
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

		//linux�ϣ���ЩresultҲ�Ǿֲ����� ����Ҫ�����ģ�����
		Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 1000000));
		Result res2 = pool.submitTask(std::make_shared<MyTask>(1, 1000000));
		pool.submitTask(std::make_shared<MyTask>(1, 1000000));
		pool.submitTask(std::make_shared<MyTask>(1, 1000000));
		pool.submitTask(std::make_shared<MyTask>(1, 1000000));
		pool.submitTask(std::make_shared<MyTask>(1, 1000000));
		//ULong sum1 = res1.get().cast_<ULong>();
		//std::cout << sum1 << std::endl;
	}//ResultҪ����
	
	std::cout << "main over!" << std::endl;
	getchar();

#if 0
	{
		ThreadPool pool;
		//�û��Լ������̳߳صĹ���ģʽ
		pool.setMode(PoolMode::MODE_CACHED);

		//�����̳߳�
		pool.start(4);

		//������result����
	
		Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 1000000));
		Result res2 = pool.submitTask(std::make_shared<MyTask>(1000001, 2000000));
		Result res3 = pool.submitTask(std::make_shared<MyTask>(2000001, 3000000));

		pool.submitTask(std::make_shared<MyTask>(2000001, 3000000));
		pool.submitTask(std::make_shared<MyTask>(2000001, 3000000));
		pool.submitTask(std::make_shared<MyTask>(2000001, 3000000));


		// ����task��ִ���꣬task����Ҳû�ˣ�������task�����result����Ҳû��
		ULong sum1 = res1.get().cast_<ULong>();//gget������һ��any���ͣ���ôת�ؾ��������
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