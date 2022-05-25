#ifndef __UNIQUEIDGENERATOR_H__
#define __UNIQUEIDGENERATOR_H__

#include <set>
// 用于生成唯一ID的类，唯一ID的作用是在客户端调用的时候挂起，等待服务端回传后进行唤醒的标志
// 虽然是模板，但是T只能是无符号整形
template<typename T>
class UniqueIdGenerator{
public:
    // 构造，初始化ID最大值，初始化脏ID标志，初始化当前ID，按位取反取得最大值(采用无符号32位整形作为id)
    UniqueIdGenerator(T maxId = ~((T)0)){
        m_maxId = maxId;
        m_bAllDirty = false;
        m_curCleanUsableId = 0;
    }
    // 返回id给id池
    void back(T& id){
        m_setDirtyId.insert(id);
    }
    // 生成一个可用id传给入参，成功返回true,失败返回false
    bool generate(T &id){
        bool ret = true;
        //在id用完以前，顺序增长，在id用完以后，从id池中选取
        if(!m_bAllDirty){
            id = m_curCleanUsableId;
            if(m_curCleanUsableId >= m_maxId){
                m_bAllDirty = true;
            }else{
                ++m_curCleanUsableId;
            }
        }else{
            if(m_setDirtyId.empty()){
                ret = false;
            }else{
                auto it = m_setDirtyId.begin();
                id = *it;
                m_setDirtyId.erase(it);
            }
        }
        return ret;
    }

private:
    //id的最大值
	T m_maxId;
	//是否所有id都是脏的，脏：id被生成过，干净：id未被生成过
	bool m_bAllDirty;
	//当前干净的可用id
	T m_curCleanUsableId;
	//脏id集合，被归还的id放于此集合
	std::set<T> m_setDirtyId;
};





#endif