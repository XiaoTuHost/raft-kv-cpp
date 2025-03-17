#pragma once
#ifndef __SKIPLIST__H__
#define __SKIPLIST__H__

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>
#include <mutex>

const std::string STORE_FILE("store/dumpfile");
static std::string delimiter = ":";

// 节点类型
template<typename K,typename V>
class Node{
    public:
        Node(){}
        Node(K k,V v,int level)
            : key(k)
            , value(v)
            , node_level(level)
        {
            // level + 1, because array index is from 0 - level
            // 数组内保存的是node*类型，指向节点
            forward = new Node<K, V> *[level + 1];
            memset(forward,0,sizeof(Node<K,V>*)*(node_level+1));
        }

        ~Node(){

            delete[] forward;

        }

        K getKey()const{return key;}
        V getValue()const{return value;}
        void set_value(V v){value=v;}

        // TODO
        // 可以理解为多个next指针指向不同层级的后继节点
        // Linear array to hold pointers to next node of different level
        // 保存的节点可能有多个层级，因此可能需要保存多个节点
        // 可以理解为一个数组内保存多个node节点，forward指向这个数组，node节点使用指针表示
        // 因此声明为二级指针
        Node<K, V> **forward;

        int node_level;
    private:
        K key;
        V value;
};

/*


并发跳表数据结构
数据+操作

*/

template<typename K,typename V>
class SkipList{
    public:
    // 操作接口
        SkipList(int);
        ~SkipList();

        int GetRandomLevel();
        // 基本数据操作
        Node<K,V>* CreateNode(const K& k,const V& v,const int& level);
        int InsertElement(const K& k,const V& v);
        void InsertAndSetElement(const K& k,const V& v);
        bool SearchElement(const K& k,V& v);
        void DeleteElement(K k);
        void DisplayList();
        int size() const { return m_element_count; }
        // 递归删除节点
        void clear(Node<K,V>*);
        
        // 文件操作/持久化
        std::string DumpFile();
        void LoadFile(const std::string &dumpfileStr);

    private:
        void GetKeyValueFromString(const std::string &str, std::string &key, std::string &value);
        bool IsValidString(const std::string & str);

    private:
    // 跳表信息
        int m_max_level;
        int m_skip_list_level;

        // 头节点
        Node<K,V> *m_header;

        // 文件操作
        //  注意是of和if f:file
        std::ofstream m_file_writer;
        std::ifstream m_file_reader;

        int m_element_count;

        std::mutex m_mutex;
        
};

/*
SkipListDump 类用于将跳表（SkipList）的键值对数据序列化，以便持久化存储或传输
*/

template<typename K,typename V>
class SkipListDump{
    public:
        friend boost::serialized::access;
        template<typename Archive>
        void serialize(Archive &ar,const unsigned int version){
            ar &m_keyDumpVec;
            ar &m_valueDumpVec;
        }
        std::vector<K> m_keyDumpVec;
        std::vector<V> m_valueDumpVec;
    public:
        void insert(const Node<K,V>& node){
            m_keyDumpVec.emplace_back(node.getKey());
            m_valueDumpVec.emplace_back(node.getValue());
        }
};

/*

查找跳表中的元素
站在前一个节点观测后一个节点
如果小于查找值，表明值在后方 往后走
如果大于查找值，表明值在后下方 先往下走
如果等于值 找到

这里将大于等于放一块写了，如果大会往下，等也会往下，找不到和等都会最终走到底退出循环
出循环后判断一下下一个节点值即可

*/
template<typename K,typename V>
bool SkipList<K,V>::SearchElement(const K& key,V& value){

    std::cout<<" --- SkipList-SearchElement --- "<<std::endl;
    
    // 表中存储指针
    // 对于dowm的另一种表达
    Node<K,V>* cur = m_header;

    for(int i=m_max_level;i>=0;i--){
        while(cur->forward[i] && cur->forward[i]->getKey()<key){
            cur=cur->forward[i];
        }
    }

    // 获取后继 比较后继值是否合法
    cur = cur->forward[0];

    if(cur && cur->getKey() == key){
        value = cur->forward[0]->getValue();
        std::cout<<" [ SkipList-SearchElement Found Key: "<<key<<" value: "<<value<<" ] "<<std::endl;
        return true;
    }
    std::cout<<" [ SkipList-SearchElement Not Found Key ]"<<std::endl;
    return false;
}

// TODO
// 可优化随机生成跳表层数
template<typename K,typename V>
int SkipList<K,V>::GetRandomLevel(){
    // 区别最大level和当前level
    int level = 1;
    while(rand()%2)
        level++;
    return level<m_max_level?level:m_max_level;
}

// 创建一个skiplist的node
template<typename K,typename V>
Node<K,V>* SkipList<K,V>::CreateNode(const K& k,const V& v,const int& level){
    return new Node<K,V>(k,v,level);
}

template<typename K,typename V>
int SkipList<K,V>::InsertElement(const K& k,const V& v){
    
    std::cout<<" --- SkipList-InsertElement --- "<<std::endl;

    // TODO 优化锁粒度
    std::lock_guard<std::mutex> lock(m_mutex);

    Node<K,V>* cur = m_header;

    // pres存放应该修改的前驱节点集合
    // 数组类型的指针 
    // 如果传递函数参数那么数组->指针
    // 即 Node<K,V>**
    // Node<K,V>* pres[m_max_level + 1];
    // memset(pres,0,sizeof(Node<K,V>*)*(m_max_level+1));

    // fixed
    std::vector<Node<K,V>*> pres(m_max_level+1,nullptr);

    // 找到每一层插入的前驱节点
    for(int i = m_skip_list_level;i>=0;i--){
        while(cur->forward[i] && cur->forward[i]->getValue()<k)
            cur=cur->forward[i];
        pres[i] = cur; 
    }

    // 后继值是否合法
    cur = cur->forward[0];

    if(cur && cur->getKey()==k){
        std::cout<<" [ SkipList-Insert Key: "<<k<<" has existed ! ] "<<std::endl;
        return -1;
    }else{
        int random_level = GetRandomLevel();
        if(random_level > m_skip_list_level){
            for(int i=m_skip_list_level+1;i<random_level+1;++i)
                pres[i]=m_header;
            // fixed 插入错误
            // 放在了条件外边，导致每次m_level在波动
            // 导致了上层节点插入的乱序
            m_skip_list_level  = random_level;
        }
        Node<K,V>* new_node = CreateNode(k,v,random_level);
        for(int i=0;i<=random_level;++i){
            new_node->forward[i] = pres[i]->forward[i];
            pres[i]->forward[i] = new_node;
        }
        std::cout<<" [ SkipList-Insert key: "<<k<<" value: "<<v<<" successfully! ]"<<std::endl;
        m_element_count++;
    }
    return 0;
}

/*
Insert 是插入元素，如果元素存在则不操作
InsertAndSer 是覆盖元素，不存在则插入，存在则覆盖
*/
template<typename K,typename V>
void SkipList<K,V>::InsertAndSetElement(const K& k,const V& v){
    V oldValue;
    if(SearchElement(k,oldValue))
        DeleteElement(k);
    InsertElement(k,v);
}

template<typename K,typename V>
void SkipList<K,V>::DeleteElement(K k){
    
    std::cout<<" --- SkipList-DeleteElement --- "<<std::endl;

    std::lock_guard<std::mutex> lock(m_mutex);

    Node<K,V>* cur = m_header;
    // Node<K,V>* pres[m_max_level+1];
    // memset(pres,0,sizeof(Node<K,V>*)*(m_max_level+1));
    std::vector<Node<K,V>*> pres(m_max_level+1,nullptr);

    for(int i=m_skip_list_level;i>=0;--i){
        while(cur->forward[i] && cur->forward[i]->getKey()<k)
            cur=cur->forward[i];
        pres[i]=cur;
    }

    cur = cur->forward[0];

    if(cur && cur->getKey()==k){
        for(int i=0;i<=m_skip_list_level;++i){
            // 在level==i可能出现下一个节点不是目标节点
            if(pres[i]->forward[i]!=cur)    break;
            pres[i]->forward[i] = cur->forward[i];
        }
        
        // 删除后 最高层为空
        // 那么减少level
        while(m_skip_list_level>0 && m_header->forward[m_skip_list_level]==0)
            m_skip_list_level--;
        std::cout<<" [ SkipList-Delete key: "<<k<<" successfully! ]"<<std::endl;
        delete cur;
        m_element_count--;
    }
}

template<typename K,typename V>
void SkipList<K,V>::DisplayList(){
    
    std::cout<<" --- Display-List -- "<<std::endl;

    // 从上往下遍历
    for(int i=m_skip_list_level;i>=0;--i){
        Node<K,V>* cur = m_header->forward[i];
        while(cur){

            // format <k1,v1> <k2,v2> <k3,v3> ......
            std::cout<<"<"<<cur->getKey()<<","<<cur->getValue()<<"> ";
            // fixed
            cur = cur -> forward[i];

        }
        std::cout<<std::endl;
    }

}

/*

析构时调用
用于递归/迭代删除所有节点

*/
template<typename K,typename V>
void SkipList<K,V>::clear(Node<K,V>* cur){

    // 针对节点操作注意加锁
    std::lock_guard<std::mutex> lock(m_mutex);

    // 递归版本
    // if(cur==nullptr) return;
    // clear(cur->forward[0]);
    // delete cur;
    
    while(cur){
        // TODO auto?
        Node<K,V>* next = cur->forward[0];
        delete  cur;
        cur=next;
    }
    // 处理参数
    this->m_skip_list_level = 0;
    this->m_element_count = 0;
    // TODO 如何优雅地处理头节点
    memset(this->m_header,0,sizeof(m_header));

}

template<typename K,typename V>
SkipList<K,V>::SkipList(int max_level){
    this->m_max_level = max_level;
    this->m_skip_list_level = 0;
    this->m_element_count = 0;
    this->m_header = new Node<K,V>(0,0,max_level);
}

template<typename K,typename V>
SkipList<K,V>::~SkipList(){

    if(m_file_reader.is_open()){
        m_file_reader.close();
    }
    if(m_file_writer.is_open()){
        m_file_writer.close();
    }
    if(m_header->forward[0]){
        clear(m_header->forward[0]);
    }

    delete (m_header);

}

template<typename K,typename V>
std::string SkipList<K,V>::DumpFile(){

    std::cout<<" --- DumpFile ---"<<std::endl;

    // 获取数据
    Node<K,V>* cur = this->m_header->forward[0];
    SkipListDump<K,V> dumper;
    while(cur){
        dumper.insert(cur);
        cur=cur->forward[0];
    }
    
    // 序列化
    std::stringstream ss;
    boost::archive::text_oarchive oa(ss);
    oa<<dumper;
    return ss.str();
}

template<typename K,typename V>
void SkipList<K,V>::LoadFile(const std::string &dumpfileStr){
    
    std::cout<<" --- LoadFile ---"<<std::endl;

    // 从序列化结果加载内容
    std::stringstream ss(dumpfileStr);
    boost::archive::text_iarchive ia(ss);
    SkipListDump<K,V> dumper;
    ia>>dumper;
    // 回放跳表
    for(int i=0;i<dumper.m_keyDumpVec.size();++i){
        InsertElement(m_keyDumpVec[i],m_valueDumpVec[i]);
    }
}

/*

可以从 "k:v"格式的字符串中获取kv值

*/
template<typename K,typename V>
void SkipList<K,V>::GetKeyValueFromString(const std::string &str, std::string &key, std::string &value){
    if(IsValidString(str)){
        int index = str.find(delimiter);
        key = str.substr(0,index);
        value = str.substr(index+1,str.length()-index-1);
    }
}

template<typename K,typename V>
bool SkipList<K,V>::IsValidString(const std::string & str){
    if(str.empty() || str.find(delimiter)==std::string::npos)
        return false;
    return true;
}

#endif