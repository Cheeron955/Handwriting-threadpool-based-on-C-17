#include "threadpool.h"

#include <functional>
#include <thread>
#include <iostream>
#include <chrono>

const int TASK_MAX_THRESHHOLD = INT32_MAX;
const int THREAD_MAX_THRESHHOLD = 100;
const int THREAD_MAX_IDLE_TIME = 60; //60s

//线程池构造
ThreadPool::ThreadPool()
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
//构造函数中没有new东西，所以不需要释放什么
//但是有构造一定要有析构
ThreadPool::~ThreadPool()
{
	isPoolRuning_ = false;
	//notEmpty_.notify_all();//把等待的叫醒 进入阻塞 会死锁

	std::unique_lock<std::mutex> lock(taskQueMtx_);
	//等待线程池里面所有的线程返回用户调用ThreadPool退出 两种状态:阻塞 正在执行任务中
	notEmpty_.notify_all();//把等待的叫醒 进入阻塞
	exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; });
}

//设置线程池的工作模式
void ThreadPool::setMode(PoolMode mode)
{
	if (checkRunningState()) return;
	poolMode_ = mode;
}

//设置task任务队列上限阈值
void ThreadPool::setTaskQueMaxThreshHold(int threshhold)
{
	if (checkRunningState()) return;
	taskQueMaxThresHold_ = threshhold;
}

//设置线程池cached模式下线程阈值
void ThreadPool::setThreadSizeThreshHold(int threshhold)
{
	if (checkRunningState()) return;

	if (poolMode_ == PoolMode::MODE_CACHED)
	{
		threadSizeThresHold_ = threshhold;
	}
}

//给线程池提交任务 用户调用改接口传入任务对象，生产任务
Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
	//获取锁
	std::unique_lock<std::mutex> lock(taskQueMtx_);

	//线程通信 等待任务队列有空余 并且用户提交任务最长不能阻塞超过1s 否则判断提交失败，返回
	//while (taskQue_.size() == taskQueMaxThresHold_){ notFull_.wait(lock);}
	//两者等价lambda表达式需要或许外部对象的成员变量需要进行变量捕获 &--引用捕获
	if(!notFull_.wait_for(lock, std::chrono::seconds(1),
		[&]()->bool {return taskQue_.size() < (size_t)taskQueMaxThresHold_; }))
	{ 
		//表示notFull_等待1s，条件依然没有满足，提交任务失败
		std::cerr << "task queue is full,submit task fail." << std::endl;
		//return task->getResult(); 不行线程执行完task task对象就被析构掉了
		return Result(sp, false);
	}

	//如果有空余，把任务放入任务队列中
	taskQue_.emplace(sp);
	taskSize_++;

	//因为新放了任务，任务队列不空了，在notEmpty_上进行通知,赶快分配线程执行任务
	notEmpty_.notify_all();

	// cached模式，任务处理比较紧急，场景：小而快的任务
	//需要根据任务数量和空闲线程的数量，判断是否需要创建新的线程出来
	if (poolMode_ == PoolMode::MODE_CACHED 
		&& taskSize_>idleThreadSize_ 
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
	return Result(sp);
}

//开启线程池
void ThreadPool::start(int initThreadSize)
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
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this,std::placeholders::_1));
		
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

//定义线程函数 线程池的所有线程从任务队列里面消费任务
void ThreadPool::threadFunc(int threadid) //线程函数返回，相应的线程就结束了
{
	auto lastTime = std::chrono::high_resolution_clock().now();

	//所有任务必须执行完成，线程池才可以回收所有线程资源
	//while (isPoolRuning_) 不可
	for(;;)
	{
		std::shared_ptr<Task> task;
		
		{
			//获取锁
			std::unique_lock<std::mutex> lock(taskQueMtx_);

			std::cout << "tid:" << std::this_thread::get_id() 
				<< "尝试获取任务..." << std::endl;

			//cached模式下，创建了很多的线程但是空闲时间超过60s，应该回收掉超过initThreadSize_的线程
			//当前时间-上一次线程执行的时间>60s
			
				//每一秒钟返回一次 超时返回？有任务待执行返回？
			//锁+双层判断
				while ( taskQue_.size() == 0 )
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

					//阻塞状态的线程被唤醒的回收  线程池要结束,回收资源
					/*if (!isPoolRuning_)
					{
						threads_.erase(threadid);
						std::cout << "threadid:" << std::this_thread::get_id()
							<< "exit!" << std::endl;

						//通知主线程线程被回收了，再次查看是否满足条件
						exitCond_.notify_all();
						return;
					}*/
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
			task->exec();
		}

		
		//线程处理完了
		idleThreadSize_++;
		
		//更新线程执行完任务调度的时间
		auto lastTime = std::chrono::high_resolution_clock().now();
	}
}

bool ThreadPool::checkRunningState() const
{
	return isPoolRuning_;
}


/////////////// 线程方法实现////////////////

int Thread::generateId_=0;

//线程构造
Thread::Thread(ThreadFunc func)
	: func_(func)
	, threadId_(generateId_++)
{
}

// 线程析构
Thread::~Thread()
{
}


//启动线程
void Thread::start()
{
	//创建一个线程来执行一个线程函数
	//c++11l来说 线程对象t 线程函数func_
	std::thread t(func_,threadId_);

	//设置分离线程 线程对象和线程函数分离，出括号线程对象挂掉不影响线程函数
	// 实际上是pthread_detach pthread_t设置成 分离线程
	t.detach();
}

int Thread::getId() const
{
	return threadId_;
}
////////////Task方法的实现///////////////
Task::Task()
	:result_(nullptr)
{}

void Task::exec()
{
	if (result_ != nullptr)
	{
		result_->setVal(run()); //多态调用
	}
	
}


void Task::setResult(Result* res)
{
	result_ = res;
}

/////////////Result方法的实现/////////////
Result::Result(std::shared_ptr<Task> task, bool isValid)
		:isValid_(isValid)
		,task_(task)
{
	task_->setResult(this);
}

Any Result::get()
{
	if (!isValid_)
	{
		return " ";
	}

	//task任务如果没有执行完，这里会阻塞用户的线程
	sem_.wait();
	return std::move(any_);
}

void Result::setVal(Any any)
{
	//存储task的返回值
	this->any_ = std::move(any);

	//已经获取了任务的返回值，增加信号量资源
	sem_.post();
}
