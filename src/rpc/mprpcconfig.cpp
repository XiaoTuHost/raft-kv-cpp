#include "mprpcconfig.h"

#include <string>
#include <iostream>
#include <fstream>

void MprpcConfig::LoadConfigFile(const char* config_file){
    std::ifstream infile(config_file);
    if(!infile.is_open()){
        std::cout << config_file << " is note exist!" << std::endl;
        exit(EXIT_FAILURE);
    }
    std::string read_line;
    while(std::getline(infile,read_line)){
        Trim(read_line);

        if(read_line.empty() || read_line[0]=='#')
            continue;
        
        if(-1==read_line.find_first_of('=')){
            continue;
        }

        auto kv = GetKV(read_line);
        auto key = kv.first;
        auto value = kv.second;
        Trim(key),Trim(value);
        m_config[key]=value;

    }

    infile.close();

}

std::string MprpcConfig::Load(const std::string &key){
    if(m_config.count(key)){
        return m_config[key];
    }
    return "";
}

/*
去掉参数str前后空格
如： k     =           v
*/
void MprpcConfig::Trim(std::string &str){
    int k_start = 0;
    int v_end = 0;
    // 前空格
    k_start = str.find_first_not_of(' ');
    if (k_start == -1) {
        return;
    }
    str = str.substr(k_start, str.length() - k_start);
    // 后空格
    v_end = str.find_last_not_of(' ');
    if (v_end == -1) {
        return;
    }
    str = str.substr(0,v_end+1);
}

std::pair<std::string,std::string> MprpcConfig::GetKV(const std::string& str){
    int index = str.find_first_of('=');
    return {str.substr(0,index),str.substr(index+1,str.length())};
}