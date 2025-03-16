#include "util.h"
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <iomanip>

void myAssert(bool condition, std::string message) {
  if (!condition) {
    std::cerr << "Error: " << message << std::endl;
    std::exit(EXIT_FAILURE);
  }
}

// high_resolution_clock 在不同标准库实现之间实现不一致，而应该避免使用它
// 通常它只是 std::chrono::steady_clock 或 std::chrono::system_clock 的别名，但实际是哪个取决于库或配置。
std::chrono::_V2::system_clock::time_point now() { return std::chrono::high_resolution_clock::now(); }

std::chrono::milliseconds getRandomizedElectionTimeout() {
  std::random_device rd; // 用于随机数引擎获得随机种子
  std::mt19937 rng(rd()); //// 以rd()为种子的标准mersenne_twister_engine
  // 分布超时时间范围
  std::uniform_int_distribution<int> dist(minRandomizedElectionTime, maxRandomizedElectionTime);
  // 在范围内生成随机的超时选举时间
  return std::chrono::milliseconds(dist(rng));
}

void sleepNMilliseconds(int N) { std::this_thread::sleep_for(std::chrono::milliseconds(N)); };

// 从给定的端口试探可用端口
// 试探大小为30
bool getReleasePort(short &port) {
  short num = 0;
  while (!isReleasePort(port) && num < 30) {
    ++port;
    ++num;
  }
  if (num >= 30) {
    port = -1;
    return false;
  }
  return true;
}

bool isReleasePort(unsigned short usPort) {
  int s = socket(PF_INET, SOCK_STREAM, IPPROTO_IP);
  sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(usPort);
  // INADDR_LOOPBACK 回环地址 127.0.0.1
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  int ret = ::bind(s, (sockaddr *)&addr, sizeof(addr));
  if (ret != 0) {
    close(s);
    return false;
  }
  close(s);
  return true;
}

void DPrintf(const char *format, ...) {
  if (Debug) {
    // 获取当前的日期，然后取日志信息，写入相应的日志文件当中 a+

    // C API
    // muduo中时间类似实现
    // 时间epoch
    time_t now = time(nullptr);
    // 格式化
    tm *nowtm = localtime(&now);
    // 可变蚕食 va_list xxx、va_satrt(xxx,xxx)、va_end(xxx)
    va_list args;
    va_start(args, format);
    // 打印时间，获取结构体的成员变量
    std::printf("[%d-%d-%d-%d-%d-%d] ", nowtm->tm_year + 1900, nowtm->tm_mon + 1, nowtm->tm_mday, nowtm->tm_hour,
                nowtm->tm_min, nowtm->tm_sec);
    // vprintf格式化输出format
    std::vprintf(format, args);
    std::printf("\n");
    va_end(args);
  }
}