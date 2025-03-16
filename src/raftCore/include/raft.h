#pragma once
#ifndef __RAFT__H__
#define __RAFT__H__

#include <boost/serialization/string.hpp>
#include <boost/serialization/vector.hpp>
#include "boost/serialization/serialization.hpp"
#include "boost/any.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "ApplyMsg.h"
#include "Persister.h"
#include "config.h"
// TODO 协程库
// 先使用线程代替
//#include "monsoon.h"
#include "raftRpcUtil.h"
#include "util.h"




#endif // !__RAFT__H__