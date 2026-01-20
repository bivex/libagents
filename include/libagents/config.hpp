#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace libagents
{

/// JSON type alias
using json = nlohmann::json;

/// Tool definition for AI to call
struct Tool
{
    std::string name;
    std::string description;
    std::string parameters_schema; // JSON schema for parameters

    /// Handler: takes JSON args, returns string result
    std::function<std::string(const std::string&)> handler;
};

/// Configuration for creating a session
struct SessionConfig
{
    bool streaming = true;     // Enable streaming by default
    std::string system_prompt; // System prompt for the AI
    std::vector<Tool> tools;   // Tools available to the AI
    std::string session_id;    // For conversation continuity

    /// Provider-specific options
    std::unordered_map<std::string, std::string> options;
};

/// Provider configuration
struct ProviderConfig
{
    std::string api_key;    // API key (if applicable)
    std::string endpoint;   // Custom endpoint (if applicable)
    int timeout_ms = 30000; // Request timeout

    /// Provider-specific options
    std::unordered_map<std::string, std::string> options;
};

/// BYOK (Bring Your Own Key) configuration
/// Allows using custom API keys instead of default provider authentication
struct BYOKConfig
{
    std::string api_key;       // API key (e.g., OpenAI key, Anthropic key)
    std::string base_url;      // API base URL (e.g., "https://api.openai.com/v1")
    std::string model;         // Model to use (e.g., "gpt-4", "claude-sonnet-4-20250514")
    std::string provider_type; // Provider type: "openai", "anthropic", "azure"
    int timeout_ms = 0;        // Request timeout in ms (0 = use default)

    /// Check if BYOK is configured (has API key)
    bool is_configured() const { return !api_key.empty(); }

    /// Load BYOK config from OpenAI environment variables
    /// Reads: OPENAI_API_KEY, OPENAI_BASE_URL, OPENAI_MODEL, API_TIMEOUT_MS
    static BYOKConfig from_openai_env();

    /// Load BYOK config from Anthropic environment variables
    /// Reads: ANTHROPIC_AUTH_TOKEN, ANTHROPIC_BASE_URL, ANTHROPIC_MODEL, API_TIMEOUT_MS
    static BYOKConfig from_anthropic_env();
};

} // namespace libagents
