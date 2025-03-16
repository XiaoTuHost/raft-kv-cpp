#pragma once
#ifndef __SKIPLIST__H__
#define __SKIPLIST__H__

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>

const std::string STORE_FILE("store/dumpfile");

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
            memset(forward,0,sizeof(Node<K,V>*(node_level+1)));
        }

        ~Node(){

            delete[] forward;

        }

        K getKey()const{return key;}
        V getValue()const{return value;}
        void set_value(V v){value=v;}

        // TODO
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

        // 基本数据操作
        int GetRandomLevel();
        Node<K,V> CreateNode(Key k,V v,int);
        int InsertElement(K k,V v);
        void DisplayList();
        bool SearchElement(K k,V v);
        void DeleteElement(K k);
        void InsertAndSerchElement(K k,V v);
        int size();
        // 递归删除节点
        void clear(Node<K,V>*);
        
        // 文件操作/持久化
        std::string DumpFile();
        void LoadFile(const std::string &dumpfile);

    private:
    // 跳表信息
        int m_max_level;
        int m_skip_list_level;

        Node<K,V> *m_header;

        // 文件操作
        std::ostream m_file_writer;
        std::istream m_file_reader;

        int m_element_count;

        std::mutex m_mutex;
        
};

#endif