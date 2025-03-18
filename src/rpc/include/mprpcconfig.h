#pragma once

#ifndef __MPRPCCONFIG__H__
#define __MPRPCCONFIG__H__

#include<string>
#include<unordered_map>

class MprpcConfig{
    public:
        void LoadConfigFile(const char* config_file);
        std::string Load(const std::string &key);

    private:
        std::unordered_map<std::string,std::string> m_config;
        void Trim(std::string &str);
        std::pair<std::string,std::string> GetKV(const std::string& str);

};
#endif