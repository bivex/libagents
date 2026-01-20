#include <cstdlib>
#include <libagents/config.hpp>

namespace libagents
{

namespace
{
// Helper to get environment variable (cross-platform)
std::string get_env(const char* name)
{
#ifdef _WIN32
    char* buf = nullptr;
    size_t len = 0;
    if (_dupenv_s(&buf, &len, name) == 0 && buf != nullptr)
    {
        std::string result(buf);
        free(buf);
        return result;
    }
    return {};
#else
    const char* val = std::getenv(name);
    return val ? val : "";
#endif
}

int get_env_int(const char* name, int default_val)
{
    std::string val = get_env(name);
    if (val.empty())
        return default_val;
    try
    {
        return std::stoi(val);
    }
    catch (...)
    {
        return default_val;
    }
}
} // namespace

BYOKConfig BYOKConfig::from_openai_env()
{
    BYOKConfig config;
    config.api_key = get_env("OPENAI_API_KEY");
    config.base_url = get_env("OPENAI_BASE_URL");
    config.model = get_env("OPENAI_MODEL");
    config.provider_type = "openai";
    config.timeout_ms = get_env_int("API_TIMEOUT_MS", 0);
    return config;
}

BYOKConfig BYOKConfig::from_anthropic_env()
{
    BYOKConfig config;
    config.api_key = get_env("ANTHROPIC_AUTH_TOKEN");
    config.base_url = get_env("ANTHROPIC_BASE_URL");
    config.model = get_env("ANTHROPIC_MODEL");
    config.provider_type = "anthropic";
    config.timeout_ms = get_env_int("API_TIMEOUT_MS", 0);
    return config;
}

} // namespace libagents
