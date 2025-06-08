/**
 * @copyright ©Chongqing SERES Phoenix Intelligent Creation Technology Co., Ltd. All rights reserved.
 * @file log.h
 * @brief  日志调试输出模块
 * @author xz
 * @version 1.0
 */
#pragma once

#include <string>
#include <sys/syscall.h>
#include <unistd.h>
#include <mutex>
#include <csignal>

enum LogLevel : uint8_t { DEBUG = 1, INFO = 2, WARN = 3, ERROR = 4, FATAL = 5 };

class LogLevelManager {
public:
    static LogLevelManager* instance()
    {
        static LogLevelManager instance;
        return &instance;
    }

    void SetLogLevel(int log_level)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        logLevel_ = log_level;
    }

    int GetLogLevel()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return logLevel_;
    }

private:
    LogLevelManager() = default;
    ~LogLevelManager() = default;
    LogLevelManager(const LogLevelManager&) = delete;
    LogLevelManager& operator=(const LogLevelManager&) = delete;

    uint32_t logLevel_{INFO}; // 默认 INFO 级别
    std::mutex mutex_;
};

/* 比较日志等级 */
#define DEBUG_CONDITION        (LogLevelManager::instance()->GetLogLevel() == LogLevel::DEBUG)
#define INFO_CONDITION         (LogLevelManager::instance()->GetLogLevel() <= LogLevel::INFO)
#define WARN_CONDITION         (LogLevelManager::instance()->GetLogLevel() <= LogLevel::WARN)
#define ERROR_CONDITION        (LogLevelManager::instance()->GetLogLevel() <= LogLevel::ERROR)
#define FATAL_CONDITION        (LogLevelManager::instance()->GetLogLevel() <= LogLevel::FATAL)

// #define RDAP_DEBUG_MODE
// #ifdef RDAP_DEBUG_MODE
// 预插入当前函数名
#define FUNCTION_FORMAT_IN_LOG << "[" << __FUNCTION__ << "] "
// #endif

/* 根据 USER_GLOG_GFLAG 选择日志实现 */
#ifdef USER_GLOG_GFLAG
#include <glog/logging.h>
#include <glog/raw_logging.h>

#define RDEBUG_             LOG_IF(INFO, DEBUG_CONDITION)
#define RINFO_              LOG_IF(INFO, INFO_CONDITION)
#define RWARN_              LOG_IF(WARNING, WARN_CONDITION)
#define RERROR_             LOG_IF(ERROR, ERROR_CONDITION)
#define RFATAL_             LOG_IF(FATAL, FATAL_CONDITION)

#define RDEBUG              RDEBUG_ FUNCTION_FORMAT_IN_LOG
#define RINFO               RINFO_ FUNCTION_FORMAT_IN_LOG
#define RWARN               RWARN_ FUNCTION_FORMAT_IN_LOG
#define RERROR              RERROR_ FUNCTION_FORMAT_IN_LOG
#define RFATAL              RFATAL_ FUNCTION_FORMAT_IN_LOG

// LOG_IF
#define RINFO_IF_(cond)     LOG_IF(INFO, (INFO_CONDITION && (cond)))
#define RERROR_IF_(cond)    LOG_IF(ERROR, (ERROR_CONDITION && (cond)))

#define RINFO_IF(cond)      RINFO_IF_(cond) FUNCTION_FORMAT_IN_LOG
#define RERROR_IF(cond)     RERROR_IF_(cond) FUNCTION_FORMAT_IN_LOG

// LOG_EVERY_N
#define RINFO_EVERY_(freq)  LOG_IF_EVERY_N(INFO, INFO_CONDITION, freq)
#define RWARN_EVERY_(freq)  LOG_IF_EVERY_N(WARNING, WARN_CONDITION, freq)
#define RERROR_EVERY_(freq) LOG_IF_EVERY_N(ERROR, ERROR_CONDITION, freq)

#define RINFO_EVERY(freq)   RINFO_EVERY_(freq) FUNCTION_FORMAT_IN_LOG
#define RWARN_EVERY(freq)   RWARN_EVERY_(freq) FUNCTION_FORMAT_IN_LOG
#define RERROR_EVERY(freq)  RERROR_EVERY_(freq) FUNCTION_FORMAT_IN_LOG

#else
#include "rclcpp/rclcpp.hpp"
#include <sstream>

class LogStream {
public:
    // 构造函数传入 logger 对象和日志级别
    LogStream(rclcpp::Logger logger, LogLevel level) : logger_(logger), level_(level)
    {}

    // 析构时，根据指定的日志级别调用相应的宏输出累积的日志内容
    ~LogStream()
    {
        const std::string msg = stream_.str();
        switch (level_) {
        case LogLevel::DEBUG:
            // RCLCPP_DEBUG(logger_, "%s", msg.c_str());
            // break;
        case LogLevel::INFO:
            RCLCPP_INFO(logger_, "%s", msg.c_str());
            break;
        case LogLevel::WARN:
            RCLCPP_WARN(logger_, "%s", msg.c_str());
            break;
        case LogLevel::ERROR:
            RCLCPP_ERROR(logger_, "%s", msg.c_str());
            break;
        case LogLevel::FATAL:
            RCLCPP_FATAL(logger_, "%s", msg.c_str());
            break;
        }
    }

    // 重载 << 操作符，支持各种类型的输入
    template <typename T>
    LogStream& operator<<(const T& value)
    {
        stream_ << value;
        return *this;
    }

    // 为 uint8_t 类型重载 << 操作符，将其强制转换为 int 输出
    LogStream& operator<<(const uint8_t& value)
    {
        stream_ << static_cast<int>(value); // 强制转换为 int 以输出数字
        return *this;
    }

    // 为 uint16_t 类型重载 << 操作符，将其强制转换为 int 输出
    LogStream& operator<<(const uint16_t& value)
    {
        stream_ << static_cast<int>(value); // 强制转换为 int 以输出数字
        return *this;
    }

private:
    rclcpp::Logger logger_;
    LogLevel level_;
    std::ostringstream stream_;
};

#define RDEBUG_                                                                                                        \
    if (DEBUG_CONDITION)                                                                                               \
    LogStream(rclcpp::get_logger("rte_rdap_node"), LogLevel::DEBUG) // 添加函数名

#define RINFO_                                                                                                         \
    if (INFO_CONDITION)                                                                                                \
    LogStream(rclcpp::get_logger("rte_rdap_node"), LogLevel::INFO) // 添加函数名

#define RWARN_                                                                                                         \
    if (WARN_CONDITION)                                                                                                \
    LogStream(rclcpp::get_logger("rte_rdap_node"), LogLevel::WARN) // 添加函数名

#define RERROR_                                                                                                        \
    if (ERROR_CONDITION)                                                                                               \
    LogStream(rclcpp::get_logger("rte_rdap_node"), LogLevel::ERROR) // 添加函数名

#define RFATAL_                                                                                                        \
    if (FATAL_CONDITION)                                                                                               \
    LogStream(rclcpp::get_logger("rte_rdap_node"), LogLevel::FATAL) // 添加函数名

#define RDEBUG RDEBUG_ FUNCTION_FORMAT_IN_LOG
#define RINFO  RINFO_ FUNCTION_FORMAT_IN_LOG
#define RWARN  RWARN_ FUNCTION_FORMAT_IN_LOG
#define RERROR RERROR_ FUNCTION_FORMAT_IN_LOG
#define RFATAL RFATAL_ FUNCTION_FORMAT_IN_LOG

// LOG_IF
#define RINFO_IF_(cond)                                                                                                \
    if (INFO_CONDITION && (cond))                                                                                      \
    LogStream(rclcpp::get_logger("rte_rdap_node"), LogLevel::INFO) // 添加函数名
#define RERROR_IF_(cond)                                                                                               \
    if (ERROR_CONDITION && (cond))                                                                                     \
    LogStream(rclcpp::get_logger("rte_rdap_node"), LogLevel::ERROR) // 添加函数名

#define RINFO_IF(cond)  RINFO_IF_(cond) FUNCTION_FORMAT_IN_LOG
#define RERROR_IF(cond) RERROR_IF_(cond) FUNCTION_FORMAT_IN_LOG

// LOG_EVERY_N
#define RINFO_EVERY_(freq)                                                                                             \
    RCLCPP_INFO_THROTTLE(                                                                                              \
        rclcpp::get_logger("rte_rdap_node"), *rclcpp::Clock::make_shared(), freq * 1000, "[%s] ", __FUNCTION__)
#define RWARN_EVERY_(freq)                                                                                             \
    RCLCPP_WARN_THROTTLE(                                                                                              \
        rclcpp::get_logger("rte_rdap_node"), *rclcpp::Clock::make_shared(), freq * 1000, "[%s] ", __FUNCTION__)
#define RERROR_EVERY_(freq)                                                                                            \
    RCLCPP_ERROR_THROTTLE(                                                                                             \
        rclcpp::get_logger("rte_rdap_node"), *rclcpp::Clock::make_shared(), freq * 1000, "[%s] ", __FUNCTION__)

#define RINFO_EVERY(freq)  RINFO_EVERY_(freq) FUNCTION_FORMAT_IN_LOG
#define RWARN_EVERY(freq)  RWARN_EVERY_(freq) FUNCTION_FORMAT_IN_LOG
#define RERROR_EVERY(freq) RERROR_EVERY_(freq) FUNCTION_FORMAT_IN_LOG
#endif // USER_GLOG_GFLAG

// 支持动态修改日志级别
#define RLOG_CHANGE_LOG_LEVEL(level) LogLevelManager::instance()->SetLogLevel(level);
