#pragma once
#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <unordered_map>
#include <thread>
#include <future>
#include <iostream>

const int TASK_MAX_THRESHHOLD = 1;//INT32_MAX;
const int THREAD_MAX_THRESHHOLD = 100;
const int THREAD_MAX_IDLE_TIME = 60; //60s

//线程池支持的模式 避免不同的枚举类型有相同的枚举项，所以需要加类型PoolMode
enum class PoolMode
{
	MODE_FIXED, //固定数量的线程
	MODE_CACHED, //线程数量可动态增长
};


// 线程类型
class Thread
{
public:
	//线程函数对象类型
	using ThreadFunc = std::function<void(int)>;

	//线程构造
	Thread(ThreadFunc func)
		: func_(func)
		, threadId_(generateId_++)
	{
	}

	// 线程析构
	~Thread() = default;

	//启动线程
	void start()
	{
		//创建一个线程来执行一个线程函数
		//c++11l来说 线程对象t 线程函数func_
		std::thread t(func_, threadId_);

		//设置分离线程 线程对象和线程函数分离，出括号线程对象挂掉不影响线程函数
		// 实际上是pthread_detach pthread_t设置成 分离线程
		t.detach();
	}

	//获取线程id
	int getId() const
	{
		return threadId_;
	}
private:

	ThreadFunc func_;

	static int generateId_;
	//保存线程对应的id
	int threadId_;
};

//静态成员变量类外初始化
int Thread::generateId_ = 0;

/*
example:
ThreadPool pool;
pool.start(4);

class MyTask : public Task
{
	public:
		void run() {// 线程代码.....}
};

pool.submitTask(std::make_shared<MyTask>());
*/

//线程池类型
class ThreadPool
{
public:

	//线程池构造
	ThreadPool()
		: initThreadSize_(4)
		, taskSize_(0)
		, idleThreadSize_(0)
		, curThreadSize_(0)
		, threadSizeThresHold_(THREAD_MAX_THRESHHOLD)
		, taskQueMaxThresHold_(TASK_MAX_THRESHHOLD)
		, poolMode_(PoolMode::MODE_FIXED)
		, isPoolRuning_(false)
	{

	}

	//线程池析构
	~ThreadPool()
	{
		isPoolRuning_ = false;
		//notEmpty_.notify_all();//把等待的叫醒 进入阻塞 会死锁

		std::unique_lock<std::mutex> lock(taskQueMtx_);
		//等待线程池里面所有的线程返回用户调用ThreadPool退出 两种状态:阻塞 正在执行任务中
		notEmpty_.notify_all();//把等待的叫醒 进入阻塞
		exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; });
	}

	//设置线程池的工作模式
	void setMode(PoolMode mode)
	{
		if (checkRunningState()) return;
		poolMode_ = mode;
	}

	//设置task任务队列上限阈值
	void setTaskQueMaxThreshHold(int threshhold)
	{
		if (checkRunningState()) return;
		taskQueMaxThresHold_ = threshhold;
	}

	//设置线程池cached模式下线程阈值
	void setThreadSizeThreshHold(int threshhold)
	{
		if (checkRunningState()) return;

		if (poolMode_ == PoolMode::MODE_CACHED)
		{
			threadSizeThresHold_ = threshhold;
		}
	}


	//给线程池提交任务
	//使用可变参模板编程，让submitTask可以接收任意任务函数和任意数量的参数
	//Result submitTask(std::shared_ptr<Task> sp);
	template<typename Func,typename... Args> //右值引用，引用折叠，可变参

	//decltype 可以推导出来表达式里面携带的类型
	//返回值future<>(<>需要填写返回值的类型)，但是目前func类型包含在Func类型里面，没有办法获得
	//func(args...) 参数传入函数，通过decltype可以推导出来类型，但是不会计算表达式
	auto submitTask(Func&& func, Args&&... args) -> std::future<decltype(func(args...))>
	{
		//打包任务 放入任务队列里面
		using RType = decltype(func(args...));  //返回值类型
		auto task = std::make_shared<std::packaged_task<RType()>>(
			std::bind(std::forward<Func>(func), std::forward<Args>(args)...)); //forward保持左值或者右值特性
		std::future<RType> result = task->get_future(); //把参数全部绑定到函数上


		//获取锁
		std::unique_lock<std::mutex> lock(taskQueMtx_);

		//线程通信 等待任务队列有空余 并且用户提交任务最长不能阻塞超过1s 否则判断提交失败，返回

		if (!notFull_.wait_for(lock, std::chrono::seconds(1),
			[&]()->bool {return taskQue_.size() < (size_t)taskQueMaxThresHold_; }))
		{
			//表示notFull_等待1s，条件依然没有满足，提交任务失败
			std::cerr << "task queue is full,submit task fail." << std::endl;
			auto task = std::make_shared<std::packaged_task<RType()>>(
				[]()->RType { return RType(); }); //任务提交失败，避免阻塞 返回0值
			(*task)(); //执行一下任务 才能得到返回值
			return task->get_future();;
		}

		//如果有空余，把任务放入任务队列中
		//taskQue_.emplace(sp);
		//using Task = std::function<void()>;任务队列的任务没有返回值，但是上面的task有返回值
		taskQue_.emplace([task]() {
			(*task)(); }); //task解引用以后就是packaged_task，然后()执行了task任务
			
		taskSize_++;

		//因为新放了任务，任务队列不空了，在notEmpty_上进行通知,赶快分配线程执行任务
		notEmpty_.notify_all();

		// cached模式，任务处理比较紧急，场景：小而快的任务
		//需要根据任务数量和空闲线程的数量，判断是否需要创建新的线程出来
		if (poolMode_ == PoolMode::MODE_CACHED
			&& taskSize_ > idleThreadSize_
			&& curThreadSize_ < threadSizeThresHold_)
		{

			std::cout << ">>> create new thread" << std::endl;

			//创建thread线程对象
			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
			//threads_.emplace_back(std::move(ptr)); //资源转移
			int threadId = ptr->getId();
			threads_.emplace(threadId, std::move(ptr));
			threads_[threadId]->start(); //启动线程

			//修改线程个数相关的变量
			curThreadSize_++;
			idleThreadSize_++;
		}

		//返回任务的Result对象
		return result;


	}

	//开启线程池 CPU核心数量
	void start(int initThreadSize = std::thread::hardware_concurrency())
	{
		//设置线程池的运行状态
		isPoolRuning_ = true;

		//记录初始线程个数
		initThreadSize_ = initThreadSize;
		curThreadSize_ = initThreadSize;

		//创建线程对象
		for (int i = 0; i < initThreadSize_; i++)
		{
			//创建thread线程对象的时候，把线程函数给到thread线程对象
			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));

			//threads_.emplace_back(ptr);不行，unique_ptr不允许左值引用拷贝
			//threads_.emplace_back(std::move(ptr)); //资源转移

			int threadId = ptr->getId();
			threads_.emplace(threadId, std::move(ptr));
		}

		//启动所有线程 std::vector<Thread*> threads_;
		for (int i = 0; i < initThreadSize_; i++)
		{
			threads_[i]->start(); //需要执行一个线程函数
			//线程函数在任务队列里查看是否有任务，然后分配给线程执行任务

			//记录初始空闲线程的数量
			idleThreadSize_++;
		}
	}

	//不允许用户拷贝和赋值
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator =(const ThreadPool&) = delete;

private:

	//定义线程函数
	void threadFunc(int threadid)
	{
		auto lastTime = std::chrono::high_resolution_clock().now();

		//所有任务必须执行完成，线程池才可以回收所有线程资源
		//while (isPoolRuning_) 不可
		for (;;)
		{
			Task task;

			{
				//获取锁
				std::unique_lock<std::mutex> lock(taskQueMtx_);

				std::cout << "tid:" << std::this_thread::get_id()
					<< "尝试获取任务..." << std::endl;

				//cached模式下，创建了很多的线程但是空闲时间超过60s，应该回收掉超过initThreadSize_的线程
				//当前时间-上一次线程执行的时间>60s

				//每一秒钟返回一次 超时返回？有任务待执行返回？
				while (taskQue_.size() == 0)
				{

					if (!isPoolRuning_)
					{
						threads_.erase(threadid);
						std::cout << "threadid:" << std::this_thread::get_id()
							<< "exit!" << std::endl;

						//通知主线程线程被回收了，再次查看是否满足条件
						exitCond_.notify_all();
						return;
					}

					if (poolMode_ == PoolMode::MODE_CACHED)
					{	//超时返回std::cv_status::timeout
						if (std::cv_status::timeout ==
							notEmpty_.wait_for(lock, std::chrono::seconds(1)))
						{
							auto now = std::chrono::high_resolution_clock().now();
							auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
							if (dur.count() >= THREAD_MAX_IDLE_TIME
								&& curThreadSize_ > initThreadSize_)
							{
								//开始回收当前线程
								//记录线程数量的相关变量的值修改
								//把线程对象从线程列表容器中删除 threadid=> thread对象=>删除
								threads_.erase(threadid);
								curThreadSize_--;
								idleThreadSize_--;

								std::cout << "threadid:" << std::this_thread::get_id()
									<< "exit!" << std::endl;

								return;
							}
						}
					}
					else
					{
						//等待notEmpty_条件
						notEmpty_.wait(lock);
					}
				}

				idleThreadSize_--;

				std::cout << "tid:" << std::this_thread::get_id()
					<< "获取任务成功..." << std::endl;

				//从任务队列中取一个任务出来
				task = taskQue_.front();
				taskQue_.pop();
				taskSize_--;

				//若依然有剩余任务，继续通知其他线程执行任务
				if (taskQue_.size() > 0)
				{
					notEmpty_.notify_all();
				}

				//取完任务，消费了一个任务 进行通知可以继续提交生产任务了
				notFull_.notify_all();


			}//释放锁,使其他线程获取任务或者提交任务

			//当前线程负责执行这个任务,基类指针指向派生类对象就执行那个对象的同名覆盖方法
			if (task != nullptr)
			{
				//task->run();//执行任务；把任务的返回值通过setVal方法给到Result
				task(); //执行std::function<void()>;
			}


			//线程处理完了
			idleThreadSize_++;

			//更新线程执行完任务调度的时间
			auto lastTime = std::chrono::high_resolution_clock().now();
		}
	}

	//检查Pool的运行状态
	bool checkRunningState() const
	{
		return isPoolRuning_;
	}

private:

	//线程列表 
	//std::vector<Thread*> threads_; 不可因为在threads_.emplace_back(new ...)
	//thread对象都是new出来的，所以需要考虑他的释放
	//使用智能指针，在vector析构时，里面元素进行析构，智能指针能够管理new出来的对象
	//std::vector<std::unique_ptr<Thread>> threads_;
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;

	//初始的线程数量 
	int initThreadSize_;

	//记录当前线程池里面线程的总数量
	std::atomic_int curThreadSize_;

	//线程数量上限阈值
	int threadSizeThresHold_;

	//记录空闲线程的数量
	std::atomic_int idleThreadSize_;

	//任务队列
	// std::queue<Task>  拷贝不行，因为任务队列中都是基类，发生多态才能使用，也就是要用指针
	// std::queue<Task*> 裸指针不行，用户可能只是创建了临时任务对象，出了submTask就被析构
	// 队列中只拿到了一个析构的对象，没有用，所以我们需要保持任务对象的生命周期，知道run函数执行结束
	//Task任务=》函数对象 不确定函数返回值类型
	using Task = std::function<void()>;
	std::queue<Task> taskQue_;

	//任务数量 需要保证线程安全
	std::atomic_int taskSize_;

	//任务队列数量上限阈值
	int taskQueMaxThresHold_;

	//任务队列互斥锁,保证任务队列的线程安全
	std::mutex taskQueMtx_;

	//表示任务队列不满
	std::condition_variable notFull_;

	//表示任务队列不空
	std::condition_variable notEmpty_;

	//等待线程资源全部回收
	std::condition_variable exitCond_;

	//当前线程池的工作模式
	PoolMode poolMode_;

	//表示当前线程池的启动状态
	std::atomic_bool isPoolRuning_;

};

#endif
