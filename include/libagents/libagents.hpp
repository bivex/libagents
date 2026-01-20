#pragma once

/// @file libagents.hpp
/// @brief Main include file for libagentscpp
///
/// Unified C++ library for AI agent providers.
/// Provides a common interface for Claude, GitHub Copilot, and future providers.
///
/// Basic usage:
/// @code
/// #include <libagents/libagents.hpp>
///
/// auto provider = libagents::create_provider(libagents::ProviderType::Claude);
/// provider->initialize();
///
/// libagents::SessionConfig config;
/// config.system_prompt = "You are a helpful assistant.";
/// config.tools.push_back({"my_tool", "Does something", "{}", handler});
///
/// auto session = provider->create_session(config);
/// auto future = session->send("Hello!");
/// std::cout << future.get() << std::endl;
/// @endcode

#include "agent.hpp"
#include "config.hpp"
#include "errors.hpp"
#include "events.hpp"
#include "provider.hpp"
#include "session.hpp"
#include "tool_builder.hpp"

// Provider-specific headers (conditionally available)
#ifdef LIBAGENTS_HAS_COPILOT
#include "providers/copilot_provider.hpp"
#endif

#ifdef LIBAGENTS_HAS_CLAUDE
#include "providers/claude_provider.hpp"
#endif

namespace libagents
{

/// Library version
constexpr const char* VERSION = "0.1.0";

/// Library version components
constexpr int VERSION_MAJOR = 0;
constexpr int VERSION_MINOR = 1;
constexpr int VERSION_PATCH = 0;

} // namespace libagents
