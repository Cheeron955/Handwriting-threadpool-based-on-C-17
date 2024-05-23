#include "threadpool.h"

#include <functional>
#include <thread>
#include <iostream>
#include <chrono>

const int TASK_MAX_THRESHHOLD = INT32_MAX;
const int THREAD_MAX_THRESHHOLD = 100;
const int THREAD_MAX_IDLE_TIME = 60; //60s

//�̳߳ع���
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

//�̳߳�����
//���캯����û��new���������Բ���Ҫ�ͷ�ʲô
//�����й���һ��Ҫ������
ThreadPool::~ThreadPool()
{
	isPoolRuning_ = false;
	//notEmpty_.notify_all();//�ѵȴ��Ľ��� �������� ������

	std::unique_lock<std::mutex> lock(taskQueMtx_);
	//�ȴ��̳߳��������е��̷߳����û�����ThreadPool�˳� ����״̬:���� ����ִ��������
	notEmpty_.notify_all();//�ѵȴ��Ľ��� ��������
	exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; });
}

//�����̳߳صĹ���ģʽ
void ThreadPool::setMode(PoolMode mode)
{
	if (checkRunningState()) return;
	poolMode_ = mode;
}

//����task�������������ֵ
void ThreadPool::setTaskQueMaxThreshHold(int threshhold)
{
	if (checkRunningState()) return;
	taskQueMaxThresHold_ = threshhold;
}

//�����̳߳�cachedģʽ���߳���ֵ
void ThreadPool::setThreadSizeThreshHold(int threshhold)
{
	if (checkRunningState()) return;

	if (poolMode_ == PoolMode::MODE_CACHED)
	{
		threadSizeThresHold_ = threshhold;
	}
}

//���̳߳��ύ���� �û����øĽӿڴ������������������
Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
	//��ȡ��
	std::unique_lock<std::mutex> lock(taskQueMtx_);

	//�߳�ͨ�� �ȴ���������п��� �����û��ύ�����������������1s �����ж��ύʧ�ܣ�����
	//while (taskQue_.size() == taskQueMaxThresHold_){ notFull_.wait(lock);}
	//���ߵȼ�lambda���ʽ��Ҫ�����ⲿ����ĳ�Ա������Ҫ���б������� &--���ò���
	if(!notFull_.wait_for(lock, std::chrono::seconds(1),
		[&]()->bool {return taskQue_.size() < (size_t)taskQueMaxThresHold_; }))
	{ 
		//��ʾnotFull_�ȴ�1s��������Ȼû�����㣬�ύ����ʧ��
		std::cerr << "task queue is full,submit task fail." << std::endl;
		//return task->getResult(); �����߳�ִ����task task����ͱ���������
		return Result(sp, false);
	}

	//����п��࣬������������������
	taskQue_.emplace(sp);
	taskSize_++;

	//��Ϊ�·�������������в����ˣ���notEmpty_�Ͻ���֪ͨ,�Ͽ�����߳�ִ������
	notEmpty_.notify_all();

	// cachedģʽ��������ȽϽ�����������С���������
	//��Ҫ�������������Ϳ����̵߳��������ж��Ƿ���Ҫ�����µ��̳߳���
	if (poolMode_ == PoolMode::MODE_CACHED 
		&& taskSize_>idleThreadSize_ 
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
	return Result(sp);
}

//�����̳߳�
void ThreadPool::start(int initThreadSize)
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
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this,std::placeholders::_1));
		
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

//�����̺߳��� �̳߳ص������̴߳��������������������
void ThreadPool::threadFunc(int threadid) //�̺߳������أ���Ӧ���߳̾ͽ�����
{
	auto lastTime = std::chrono::high_resolution_clock().now();

	//�����������ִ����ɣ��̳߳زſ��Ի��������߳���Դ
	//while (isPoolRuning_) ����
	for(;;)
	{
		std::shared_ptr<Task> task;
		
		{
			//��ȡ��
			std::unique_lock<std::mutex> lock(taskQueMtx_);

			std::cout << "tid:" << std::this_thread::get_id() 
				<< "���Ի�ȡ����..." << std::endl;

			//cachedģʽ�£������˺ܶ���̵߳��ǿ���ʱ�䳬��60s��Ӧ�û��յ�����initThreadSize_���߳�
			//��ǰʱ��-��һ���߳�ִ�е�ʱ��>60s
			
				//ÿһ���ӷ���һ�� ��ʱ���أ��������ִ�з��أ�
			//��+˫���ж�
				while ( taskQue_.size() == 0 )
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

					//����״̬���̱߳����ѵĻ���  �̳߳�Ҫ����,������Դ
					/*if (!isPoolRuning_)
					{
						threads_.erase(threadid);
						std::cout << "threadid:" << std::this_thread::get_id()
							<< "exit!" << std::endl;

						//֪ͨ���߳��̱߳������ˣ��ٴβ鿴�Ƿ���������
						exitCond_.notify_all();
						return;
					}*/
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
			task->exec();
		}

		
		//�̴߳�������
		idleThreadSize_++;
		
		//�����߳�ִ����������ȵ�ʱ��
		auto lastTime = std::chrono::high_resolution_clock().now();
	}
}

bool ThreadPool::checkRunningState() const
{
	return isPoolRuning_;
}


/////////////// �̷߳���ʵ��////////////////

int Thread::generateId_=0;

//�̹߳���
Thread::Thread(ThreadFunc func)
	: func_(func)
	, threadId_(generateId_++)
{
}

// �߳�����
Thread::~Thread()
{
}


//�����߳�
void Thread::start()
{
	//����һ���߳���ִ��һ���̺߳���
	//c++11l��˵ �̶߳���t �̺߳���func_
	std::thread t(func_,threadId_);

	//���÷����߳� �̶߳�����̺߳������룬�������̶߳���ҵ���Ӱ���̺߳���
	// ʵ������pthread_detach pthread_t���ó� �����߳�
	t.detach();
}

int Thread::getId() const
{
	return threadId_;
}
////////////Task������ʵ��///////////////
Task::Task()
	:result_(nullptr)
{}

void Task::exec()
{
	if (result_ != nullptr)
	{
		result_->setVal(run()); //��̬����
	}
	
}


void Task::setResult(Result* res)
{
	result_ = res;
}

/////////////Result������ʵ��/////////////
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

	//task�������û��ִ���꣬����������û����߳�
	sem_.wait();
	return std::move(any_);
}

void Result::setVal(Any any)
{
	//�洢task�ķ���ֵ
	this->any_ = std::move(any);

	//�Ѿ���ȡ������ķ���ֵ�������ź�����Դ
	sem_.post();
}
