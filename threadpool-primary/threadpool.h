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

//Any���ͣ����Խ���������������
class Any
{
public:
	Any() = default;
	~Any() = default;

	//��ֵ
	Any(const Any&) = delete;
	Any& operator=(const Any&) = delete;

	//��ֵ
	Any(Any&&) = default;
	Any& operator=(Any&&) = default;

	template<typename T>

	// ����ָ��ָ����������� ���캯��������Any���ͽ���������������������
	// �����������������
	Any(T data) :base_(std::make_unique<Derive<T>>(data))
	{}

	//��Any��������洢��data������ȡ����
	template<typename T>
	T cast_()
	{
		//��ô��base_�ҵ���ָ���Derive���󣬴�������ȡ��data��Ա������
		//����ָ�� =�� ������ָ�� dynamic_cast������ָ�������ת��Ϊ������ָ�������
		//ת���ɹ���pd����һ����Ч��ָ�룻���ת��ʧ�� pd ���� nullptr��
		Derive<T> *pd = dynamic_cast<Derive<T>*>(base_.get());
		if (pd == nullptr)
		{
			throw "type is unmatch";
		}
		return pd->data_;
	}

private:
	//��������
	class Base
	{
	public:
		virtual ~Base() = default;
	};

	//����������
	template<typename T>//ģ��
	class Derive :public Base
	{
	public:
		Derive(T data) : data_(data)
		{}
		T data_; //�������������������
	};

private:
	//����һ������ָ�룬����ָ�����ָ�����������
	std::unique_ptr<Base> base_;
};


//ʵ��һ���ź�����
class Semaphore
{
public:
	Semaphore(int limit = 0)
		:resLimit_(limit)
	{}

	~Semaphore() = default;

	//��ȡһ���ź�����Դ
	void wait()
	{  
		std::unique_lock<std::mutex> lock(mtx_);
		//�ȴ��ź�������Դ û����Դ�Ļ� ��������ǰ�߳�
		cond_.wait(lock, [&]()->bool { return resLimit_ > 0; });
		resLimit_--;
	}

	//����һ���ź�����Դ
	void post()
	{
		std::unique_lock<std::mutex> lock(mtx_);
		resLimit_++;

		//����linux��û���ͷ���Դ��������linux������״̬ʧЧ ������
		cond_.notify_all();
	}
private:

	int resLimit_;
	std::mutex mtx_;
	std::condition_variable cond_;

};

//Task�����ǰ������
class Task;

//ʵ�ֽ����ύ���̳߳ص�task����ִ����ɺ�ķ���ֵ����result
class Result
{
public:

	Result(std::shared_ptr<Task> task, bool isValid = true);
	~Result() = default;

	//setVal��������ȡ����ִ����ķ���ֵ
	void setVal(Any any);

	//�û�����get��������ȡtask�ķ���ֵ
	Any get();
private:
	//�洢����ķ���ֵ
	Any any_;

	//�߳�ͨ���ź���
	Semaphore sem_;

	//ָ���Ӧ��ȡ����ֵ���������
	std::shared_ptr<Task> task_;

	//����ֵ�Ƿ���Ч
	std::atomic_bool isValid_;
};

// ����������
class Task
{
public:

	Task();
	~Task()=default;

	void exec();

	void setResult(Result*res);

	//�û������Զ��������������ͣ���Task�̳У���дrun������ʵ���Զ���������
	virtual Any run() = 0;
private:
	Result* result_; //Result���������ڡ�Task��
};

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
	Thread(ThreadFunc func);

	// �߳�����
	~Thread();
	
	//�����߳�
	void start();

	//��ȡ�߳�id
	int getId() const;
private:

	ThreadFunc func_;

	static int generateId_;
	//�����̶߳�Ӧ��id
	int threadId_;
};

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
	ThreadPool();

	//�̳߳�����
	~ThreadPool();

	//�����̳߳صĹ���ģʽ
	void setMode(PoolMode mode);

	//����task�������������ֵ
	void setTaskQueMaxThreshHold(int threshhold);

	//�����̳߳�cachedģʽ���߳���ֵ
	void setThreadSizeThreshHold(int threshhold);

	//���̳߳��ύ����
	Result submitTask(std::shared_ptr<Task> sp);

	//�����̳߳� CPU��������
	void start(int initThreadSize = std::thread::hardware_concurrency());

	//�������û������͸�ֵ
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator =(const ThreadPool&) = delete;

private:
	
	//�����̺߳���
	void threadFunc(int threadid);

	//���Pool������״̬
	bool checkRunningState() const;

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
	std::queue<std::shared_ptr<Task>> taskQue_;

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
