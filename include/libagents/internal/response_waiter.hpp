#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>

namespace libagents::detail
{

struct ResponseWaitResult
{
    bool completed = false;
    std::string response;
    std::string error_message;
};

class ResponseWaiter
{
  public:
    void append_response(const std::string& chunk)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        response_ += chunk;
    }

    void set_error(std::string message)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        error_message_ = std::move(message);
        done_ = true;
        cv_.notify_one();
    }

    void mark_done()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        done_ = true;
        cv_.notify_one();
    }

    ResponseWaitResult wait_for(std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        bool completed = cv_.wait_for(lock, timeout, [this]() { return done_; });
        ResponseWaitResult result;
        result.completed = completed;
        result.response = response_;
        result.error_message = error_message_;
        return result;
    }

  private:
    std::mutex mutex_;
    std::condition_variable cv_;
    bool done_ = false;
    std::string response_;
    std::string error_message_;
};

} // namespace libagents::detail
