#pragma once

#include <future>
#include <memory>
#include <string>

#include "config.hpp"
#include "events.hpp"

namespace libagents
{

/// Session interface for interacting with an AI agent
class ISession
{
  public:
    virtual ~ISession() = default;

    /// Send a message to the AI
    /// For streaming: returns immediately, events delivered via callback
    /// For non-streaming: blocks until complete, returns full response
    virtual std::future<std::string> send(const std::string& message) = 0;

    /// Subscribe to events (for streaming mode)
    /// Returns subscription handle - unsubscribes on destruction
    virtual std::unique_ptr<Subscription> on(EventCallback callback) = 0;

    /// Get accumulated response (for non-streaming or post-stream)
    virtual std::string get_response() const = 0;

    /// Abort current request
    virtual void abort() = 0;

    /// Check if streaming is enabled
    virtual bool is_streaming() const = 0;

    /// Get session ID (may be assigned by provider)
    virtual std::string session_id() const = 0;

    /// Check if session is currently processing a request
    virtual bool is_busy() const = 0;
};

} // namespace libagents
