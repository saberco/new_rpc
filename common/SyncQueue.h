#ifndef __SYNCQUEUE_H__
#define __SYNCQUEUE_H__
// 阻塞队列类，提供两种方式——阻塞和非阻塞访问
// 采用双向环形链表实现
// 采用boost库避免封装pthread的线程同步类，boost自带的是raii的
#include <list>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition_variable.hpp>

template<typename T>
class SyncQueue{
public:
    //构造函数，是否以阻塞的方式进行队列的访问
    SyncQueue(bool bWait = false){
        m_maxSize = 0;
        m_bStopped = false;
        m_bwait = bWait;
    }
    //构造函数，设置最大的队列长度
    SyncQueue(unsigned int maxSize,bool bWait = false){
        m_maxSize = maxSize;
        m_bStopped = false;
        m_bwait = bWait;
    }

    //向队列中放一个左值
    bool put(T &x){
        add(x, true);
    }

    //向队列中放一个右值
    bool put(T &&x){
        add(x, false);
    }

    //从阻塞队列中取一个值
    bool take(T &x){
        //非阻塞队列为空直接返回
        if(!m_bwait && m_queue.empty()){
            return false;
        }
        //加锁
        boost::unique_lock<boost:mutex> lock(m_mutex);
        if(m_bwait){
            //没有调用stop，等待
            while(!m_bStopped && m_queue.empty()){
                m_notEmpty.wait(lock);
            }
            if(m_queue.empty()){
                return false;
            }
        }else{
            if(m_queue.empty()){
                return false;
            }
        }
        //接下来从队列中取值
        x = m_queue.front();
        m_queue.pop_front();
        if(m_bwait){
            m_notFull.notify_one();
        }
        return true;
    }

    //取出阻塞队列中的所有值
    bool takeAll(std::list<T> &queue){
        if(!m_bwait && m_queue.empty()){
            return false;
        }
        //boost::unique_lock对象会自动解锁
        boost::unique_lock<boost::mutex> lock(m_lock);
        if(m_bwait){
            while(!m_bStopped && m_queue.empty()){
                m_notEmpty.wait(lock);
            }
            if(m_queue.empty()){
                return false;
            }
        }else{
            if(m_queue.empty()){
                return false;
            }
        }
        queue = m_queue;
        m_queue.clear();
        if(m_bwait){
            m_notFull.notify_one();
        }
        return true;
    }

    //唤醒所有线程，并以后只能以非阻塞的形式访问
    void stop(){
        if(m_bwait){
            m_bStopped = true;
            //唤醒所有等待条件的线程
            m_notEmpty.notify_all();
            m_notFull.notify_all();
        }
    }

    //设置最大队列值
    //得加锁
    void setMaxSize(unsigned int maxSize){
        boost::unique_lock<boost::mutex> lock(m_lock);
        m_maxSize = maxSize;
    }



    //更改访问方式
    void setWait(bool bWait){
        boost::unique_lock<boost::mutex> lock(m_lock);
        m_bwait = bWait;
    }
    //获得当前长度
    unsigned int getSize() const
	{
		return m_queue.size();
	}

private:

    //向队列中添加一个值，内部调用
    //x 是具体的值，bLeft用来判断给的是左值还是右值，右值采用move，左值直接Push
    //成功返回true,失败返回false
    bool add(T &x, bool bLeft){
        //如果非阻塞，队列满直接返回
        if(!m_bwait && m_maxSize <= m_queue.size()){
            return false;
        }
        //阻塞队列是临界区资源，加锁访问
        boost::unique_lock<boost::mutex> lock(m_mutex);
        //如果是阻塞访问，利用条件变量进行线程阻塞
        if(m_bwait){
            //如果没有说stop,等待非满
            while(!m_bStopped && m_maxSize <= m_queue.size()){
                m_notFull.wait(lock);
            }
            //进行二次判断
            if(m_maxSize <= m_queue.size()){
                return false;
            }
        }//进行二次判断，多线程
        else{
            if(m_maxSize <= m_queue.size()){
                return false;
            }
        }
        //此时，队列已经非满，可以向其中放置值
        if(bLeft){
            m_queue.push_back(x);
        }else{
            m_queue.push_back(move(x));
        }
        //放置完毕后，唤醒等待非空条件的线程
        if(m_bwait){
            m_notEmpty.notify_one();
        }
        return true;
    }


    //队列
    std::list<T> m_queue;
    //队列最大长度
    unsigned int m_maxSize;
    //是否以阻塞的方式访问
    bool m_bwait;
    //
    bool m_bStopped;
    //互斥量
    boost::mutex m_mutex;
    //队列非空条件变量
    boost::condition_variable m_notEmpty;
    //队列非满条件变量
    boost::condition_variable m_notFull;
};














#endif