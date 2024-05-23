#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <thread>
#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <unordered_map>

//Any类型：可以接受任意数据类型
class Any
{
public:
	Any() = default;
	~Any() = default;

	//左值
	Any(const Any&) = delete;
	Any& operator=(const Any&) = delete;

	//右值
	Any(Any&&) = default;
	Any& operator=(Any&&) = default;

	template<typename T>

	// 基类指针指向派生类对象 构造函数可以让Any类型接收任意其他的数据类型
	// 包在派生类对象里面
	Any(T data) :base_(std::make_unique<Derive<T>>(data))
	{}

	//把Any对象里面存储的data数据提取出来
	template<typename T>
	T cast_()
	{
		//怎么从base_找到它指向的Derive对象，从它里面取出data成员变量？
		//基类指针 =》 派生类指针 dynamic_cast将基类指针或引用转换为派生类指针或引用
		//转换成功，pd将是一个有效的指针；如果转换失败 pd 将是 nullptr。
		Derive<T> *pd = dynamic_cast<Derive<T>*>(base_.get());
		if (pd == nullptr)
		{
			throw "type is unmatch";
		}
		return pd->data_;
	}

private:
	//基类类型
	class Base
	{
	public:
		virtual ~Base() = default;
	};

	//派生类类型
	template<typename T>//模板
	class Derive :public Base
	{
	public:
		Derive(T data) : data_(data)
		{}
		T data_; //保存了任意的其他类型
	};

private:
	//定义一个基类指针，基类指针可以指向派生类对象
	std::unique_ptr<Base> base_;
};


//实现一个信号量类
class Semaphore
{
public:
        Semaphore(int limit = 0)
                :resLimit_(limit)
                ,isExit_(false)
        {}

        ~Semaphore()
        {
                isExit_ = true;
        }

        //»ñÈ¡Ò»¸öÐÅºÅÁ¿×ÊÔ´
        void wait()
        {
                if(isExit_) return;
                std::unique_lock<std::mutex> lock(mtx_);
                //µÈ´ýÐÅºÅÁ¿ÓÐ×ÊÔ´ Ã»ÓÐ×ÊÔ´µÄ»° »á×èÈûµ±Ç°Ïß³Ì
                cond_.wait(lock, [&]()->bool { return resLimit_ > 0; });
                resLimit_--;
        }

        //Ôö¼ÓÒ»¸öÐÅºÅÁ¿×ÊÔ´
        void post()
        {
                if(isExit_) return;
                std::unique_lock<std::mutex> lock(mtx_);
                resLimit_++;
                cond_.notify_all();
        }
private:
        std::atomic_bool isExit_;
        int resLimit_;
        std::mutex mtx_;
        std::condition_variable cond_;

};

//Task对象的前置声明
class Task;

//实现接受提交到线程池的task任务执行完成后的返回值类型result
class Result
{
public:

	Result(std::shared_ptr<Task> task, bool isValid = true);
	~Result() = default;

	//setVal方法，获取任务执行完的返回值
	void setVal(Any any);

	//用户调用get方法，获取task的返回值
	Any get();
private:
	//存储任务的返回值
	Any any_;

	//线程通信信号量
	Semaphore sem_;

	//指向对应获取返回值的任务对象
	std::shared_ptr<Task> task_;

	//返回值是否有效
	std::atomic_bool isValid_;
};

// 任务抽象基类
class Task
{
public:

	Task();
	~Task()=default;

	void exec();

	void setResult(Result*res);

	//用户可以自定义任意任务类型，从Task继承，重写run方法，实现自定义任务处理
	virtual Any run() = 0;
private:
	Result* result_; //Result的生命周期》Task的
};

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
	Thread(ThreadFunc func);

	// 线程析构
	~Thread();
	
	//启动线程
	void start();

	//获取线程id
	int getId() const;
private:

	ThreadFunc func_;

	static int generateId_;
	//保存线程对应的id
	int threadId_;
};

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
	ThreadPool();

	//线程池析构
	~ThreadPool();

	//设置线程池的工作模式
	void setMode(PoolMode mode);

	//设置task任务队列上限阈值
	void setTaskQueMaxThreshHold(int threshhold);

	//设置线程池cached模式下线程阈值
	void setThreadSizeThreshHold(int threshhold);

	//给线程池提交任务
	Result submitTask(std::shared_ptr<Task> sp);

	//开启线程池 CPU核心数量
	void start(int initThreadSize = std::thread::hardware_concurrency());

	//不允许用户拷贝和赋值
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator =(const ThreadPool&) = delete;

private:
	
	//定义线程函数
	void threadFunc(int threadid);

	//检查Pool的运行状态
	bool checkRunningState() const;

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
	std::queue<std::shared_ptr<Task>> taskQue_;

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
