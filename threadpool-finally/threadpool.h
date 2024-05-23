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

//�̳߳�֧�ֵ�ģʽ ���ⲻͬ��ö����������ͬ��ö���������Ҫ������PoolMode
enum class PoolMode
{
	MODE_FIXED, //�̶��������߳�
	MODE_CACHED, //�߳������ɶ�̬����
};


// �߳�����
class Thread
{
public:
	//�̺߳�����������
	using ThreadFunc = std::function<void(int)>;

	//�̹߳���
	Thread(ThreadFunc func)
		: func_(func)
		, threadId_(generateId_++)
	{
	}

	// �߳�����
	~Thread() = default;

	//�����߳�
	void start()
	{
		//����һ���߳���ִ��һ���̺߳���
		//c++11l��˵ �̶߳���t �̺߳���func_
		std::thread t(func_, threadId_);

		//���÷����߳� �̶߳�����̺߳������룬�������̶߳���ҵ���Ӱ���̺߳���
		// ʵ������pthread_detach pthread_t���ó� �����߳�
		t.detach();
	}

	//��ȡ�߳�id
	int getId() const
	{
		return threadId_;
	}
private:

	ThreadFunc func_;

	static int generateId_;
	//�����̶߳�Ӧ��id
	int threadId_;
};

//��̬��Ա���������ʼ��
int Thread::generateId_ = 0;

/*
example:
ThreadPool pool;
pool.start(4);

class MyTask : public Task
{
	public:
		void run() {// �̴߳���.....}
};

pool.submitTask(std::make_shared<MyTask>());
*/

//�̳߳�����
class ThreadPool
{
public:

	//�̳߳ع���
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

	//�̳߳�����
	~ThreadPool()
	{
		isPoolRuning_ = false;
		//notEmpty_.notify_all();//�ѵȴ��Ľ��� �������� ������

		std::unique_lock<std::mutex> lock(taskQueMtx_);
		//�ȴ��̳߳��������е��̷߳����û�����ThreadPool�˳� ����״̬:���� ����ִ��������
		notEmpty_.notify_all();//�ѵȴ��Ľ��� ��������
		exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; });
	}

	//�����̳߳صĹ���ģʽ
	void setMode(PoolMode mode)
	{
		if (checkRunningState()) return;
		poolMode_ = mode;
	}

	//����task�������������ֵ
	void setTaskQueMaxThreshHold(int threshhold)
	{
		if (checkRunningState()) return;
		taskQueMaxThresHold_ = threshhold;
	}

	//�����̳߳�cachedģʽ���߳���ֵ
	void setThreadSizeThreshHold(int threshhold)
	{
		if (checkRunningState()) return;

		if (poolMode_ == PoolMode::MODE_CACHED)
		{
			threadSizeThresHold_ = threshhold;
		}
	}


	//���̳߳��ύ����
	//ʹ�ÿɱ��ģ���̣���submitTask���Խ������������������������Ĳ���
	//Result submitTask(std::shared_ptr<Task> sp);
	template<typename Func,typename... Args> //��ֵ���ã������۵����ɱ��

	//decltype �����Ƶ��������ʽ����Я��������
	//����ֵfuture<>(<>��Ҫ��д����ֵ������)������Ŀǰfunc���Ͱ�����Func�������棬û�а취���
	//func(args...) �������뺯����ͨ��decltype�����Ƶ��������ͣ����ǲ��������ʽ
	auto submitTask(Func&& func, Args&&... args) -> std::future<decltype(func(args...))>
	{
		//������� ���������������
		using RType = decltype(func(args...));  //����ֵ����
		auto task = std::make_shared<std::packaged_task<RType()>>(
			std::bind(std::forward<Func>(func), std::forward<Args>(args)...)); //forward������ֵ������ֵ����
		std::future<RType> result = task->get_future(); //�Ѳ���ȫ���󶨵�������


		//��ȡ��
		std::unique_lock<std::mutex> lock(taskQueMtx_);

		//�߳�ͨ�� �ȴ���������п��� �����û��ύ�����������������1s �����ж��ύʧ�ܣ�����

		if (!notFull_.wait_for(lock, std::chrono::seconds(1),
			[&]()->bool {return taskQue_.size() < (size_t)taskQueMaxThresHold_; }))
		{
			//��ʾnotFull_�ȴ�1s��������Ȼû�����㣬�ύ����ʧ��
			std::cerr << "task queue is full,submit task fail." << std::endl;
			auto task = std::make_shared<std::packaged_task<RType()>>(
				[]()->RType { return RType(); }); //�����ύʧ�ܣ��������� ����0ֵ
			(*task)(); //ִ��һ������ ���ܵõ�����ֵ
			return task->get_future();;
		}

		//����п��࣬������������������
		//taskQue_.emplace(sp);
		//using Task = std::function<void()>;������е�����û�з���ֵ�����������task�з���ֵ
		taskQue_.emplace([task]() {
			(*task)(); }); //task�������Ժ����packaged_task��Ȼ��()ִ����task����
			
		taskSize_++;

		//��Ϊ�·�������������в����ˣ���notEmpty_�Ͻ���֪ͨ,�Ͽ�����߳�ִ������
		notEmpty_.notify_all();

		// cachedģʽ��������ȽϽ�����������С���������
		//��Ҫ�������������Ϳ����̵߳��������ж��Ƿ���Ҫ�����µ��̳߳���
		if (poolMode_ == PoolMode::MODE_CACHED
			&& taskSize_ > idleThreadSize_
			&& curThreadSize_ < threadSizeThresHold_)
		{

			std::cout << ">>> create new thread" << std::endl;

			//����thread�̶߳���
			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
			//threads_.emplace_back(std::move(ptr)); //��Դת��
			int threadId = ptr->getId();
			threads_.emplace(threadId, std::move(ptr));
			threads_[threadId]->start(); //�����߳�

			//�޸��̸߳�����صı���
			curThreadSize_++;
			idleThreadSize_++;
		}

		//���������Result����
		return result;


	}

	//�����̳߳� CPU��������
	void start(int initThreadSize = std::thread::hardware_concurrency())
	{
		//�����̳߳ص�����״̬
		isPoolRuning_ = true;

		//��¼��ʼ�̸߳���
		initThreadSize_ = initThreadSize;
		curThreadSize_ = initThreadSize;

		//�����̶߳���
		for (int i = 0; i < initThreadSize_; i++)
		{
			//����thread�̶߳����ʱ�򣬰��̺߳�������thread�̶߳���
			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));

			//threads_.emplace_back(ptr);���У�unique_ptr��������ֵ���ÿ���
			//threads_.emplace_back(std::move(ptr)); //��Դת��

			int threadId = ptr->getId();
			threads_.emplace(threadId, std::move(ptr));
		}

		//���������߳� std::vector<Thread*> threads_;
		for (int i = 0; i < initThreadSize_; i++)
		{
			threads_[i]->start(); //��Ҫִ��һ���̺߳���
			//�̺߳��������������鿴�Ƿ�������Ȼ�������߳�ִ������

			//��¼��ʼ�����̵߳�����
			idleThreadSize_++;
		}
	}

	//�������û������͸�ֵ
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator =(const ThreadPool&) = delete;

private:

	//�����̺߳���
	void threadFunc(int threadid)
	{
		auto lastTime = std::chrono::high_resolution_clock().now();

		//�����������ִ����ɣ��̳߳زſ��Ի��������߳���Դ
		//while (isPoolRuning_) ����
		for (;;)
		{
			Task task;

			{
				//��ȡ��
				std::unique_lock<std::mutex> lock(taskQueMtx_);

				std::cout << "tid:" << std::this_thread::get_id()
					<< "���Ի�ȡ����..." << std::endl;

				//cachedģʽ�£������˺ܶ���̵߳��ǿ���ʱ�䳬��60s��Ӧ�û��յ�����initThreadSize_���߳�
				//��ǰʱ��-��һ���߳�ִ�е�ʱ��>60s

				//ÿһ���ӷ���һ�� ��ʱ���أ��������ִ�з��أ�
				while (taskQue_.size() == 0)
				{

					if (!isPoolRuning_)
					{
						threads_.erase(threadid);
						std::cout << "threadid:" << std::this_thread::get_id()
							<< "exit!" << std::endl;

						//֪ͨ���߳��̱߳������ˣ��ٴβ鿴�Ƿ���������
						exitCond_.notify_all();
						return;
					}

					if (poolMode_ == PoolMode::MODE_CACHED)
					{	//��ʱ����std::cv_status::timeout
						if (std::cv_status::timeout ==
							notEmpty_.wait_for(lock, std::chrono::seconds(1)))
						{
							auto now = std::chrono::high_resolution_clock().now();
							auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
							if (dur.count() >= THREAD_MAX_IDLE_TIME
								&& curThreadSize_ > initThreadSize_)
							{
								//��ʼ���յ�ǰ�߳�
								//��¼�߳���������ر�����ֵ�޸�
								//���̶߳�����߳��б�������ɾ�� threadid=> thread����=>ɾ��
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
						//�ȴ�notEmpty_����
						notEmpty_.wait(lock);
					}
				}

				idleThreadSize_--;

				std::cout << "tid:" << std::this_thread::get_id()
					<< "��ȡ����ɹ�..." << std::endl;

				//�����������ȡһ���������
				task = taskQue_.front();
				taskQue_.pop();
				taskSize_--;

				//����Ȼ��ʣ�����񣬼���֪ͨ�����߳�ִ������
				if (taskQue_.size() > 0)
				{
					notEmpty_.notify_all();
				}

				//ȡ������������һ������ ����֪ͨ���Լ����ύ����������
				notFull_.notify_all();


			}//�ͷ���,ʹ�����̻߳�ȡ��������ύ����

			//��ǰ�̸߳���ִ���������,����ָ��ָ������������ִ���Ǹ������ͬ�����Ƿ���
			if (task != nullptr)
			{
				//task->run();//ִ�����񣻰�����ķ���ֵͨ��setVal��������Result
				task(); //ִ��std::function<void()>;
			}


			//�̴߳�������
			idleThreadSize_++;

			//�����߳�ִ����������ȵ�ʱ��
			auto lastTime = std::chrono::high_resolution_clock().now();
		}
	}

	//���Pool������״̬
	bool checkRunningState() const
	{
		return isPoolRuning_;
	}

private:

	//�߳��б� 
	//std::vector<Thread*> threads_; ������Ϊ��threads_.emplace_back(new ...)
	//thread������new�����ģ�������Ҫ���������ͷ�
	//ʹ������ָ�룬��vector����ʱ������Ԫ�ؽ�������������ָ���ܹ�����new�����Ķ���
	//std::vector<std::unique_ptr<Thread>> threads_;
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;

	//��ʼ���߳����� 
	int initThreadSize_;

	//��¼��ǰ�̳߳������̵߳�������
	std::atomic_int curThreadSize_;

	//�߳�����������ֵ
	int threadSizeThresHold_;

	//��¼�����̵߳�����
	std::atomic_int idleThreadSize_;

	//�������
	// std::queue<Task>  �������У���Ϊ��������ж��ǻ��࣬������̬����ʹ�ã�Ҳ����Ҫ��ָ��
	// std::queue<Task*> ��ָ�벻�У��û�����ֻ�Ǵ�������ʱ������󣬳���submTask�ͱ�����
	// ������ֻ�õ���һ�������Ķ���û���ã�����������Ҫ�������������������ڣ�֪��run����ִ�н���
	//Task����=���������� ��ȷ����������ֵ����
	using Task = std::function<void()>;
	std::queue<Task> taskQue_;

	//�������� ��Ҫ��֤�̰߳�ȫ
	std::atomic_int taskSize_;

	//�����������������ֵ
	int taskQueMaxThresHold_;

	//������л�����,��֤������е��̰߳�ȫ
	std::mutex taskQueMtx_;

	//��ʾ������в���
	std::condition_variable notFull_;

	//��ʾ������в���
	std::condition_variable notEmpty_;

	//�ȴ��߳���Դȫ������
	std::condition_variable exitCond_;

	//��ǰ�̳߳صĹ���ģʽ
	PoolMode poolMode_;

	//��ʾ��ǰ�̳߳ص�����״̬
	std::atomic_bool isPoolRuning_;

};

#endif
