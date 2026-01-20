/// @file basic_agent.cpp
/// @brief Simplest possible libagents example
///
/// Usage: basic_agent [claude|copilot]

#include <libagents/libagents.hpp>
#include <iostream>
#include <string>

int main(int argc, char* argv[])
{
    // Parse provider from command line (default: claude)
    auto provider = libagents::ProviderType::Claude;
    if (argc > 1)
        provider = libagents::parse_provider_type(argv[1]);

    std::cout << "Using provider: " << libagents::provider_type_name(provider) << std::endl;

    // Create agent
    auto agent = libagents::create_agent(provider);

    // Register a simple tool
    agent->register_tool(libagents::make_tool(
        "get_time", "Get current time",
        []() { return "2025-01-20 12:00:00"; },
        {}
    ));

    // Initialize and query
    agent->set_system_prompt("You are a helpful assistant. Be concise.");
    if (!agent->initialize())
    {
        std::cerr << "Failed to initialize agent" << std::endl;
        return 1;
    }

    std::string response = agent->query("What time is it? Use the get_time tool.");
    std::cout << response << std::endl;

    return 0;
}
