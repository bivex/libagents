/// @file hosted_query.cpp
/// @brief Demonstrates query_hosted: tool execution on the main thread
///
/// Why this matters:
/// - Debugger APIs often require calls from a specific thread
/// - UI updates must happen on the main thread
/// - Some libraries aren't thread-safe
///
/// query_hosted() solves this by dispatching tool calls back to the caller.
///
/// Usage: hosted_query [claude|copilot]

#include <libagents/libagents.hpp>
#include <iostream>
#include <string>

// Simulated "context" that must be accessed from main thread only
struct AppContext
{
    int counter = 0;

    void increment()
    {
        counter++;
        std::cout << "  [main thread] counter = " << counter << std::endl;
    }
};

int main(int argc, char* argv[])
{
    // Parse provider from command line (default: claude)
    auto provider = libagents::ProviderType::Claude;
    if (argc > 1)
        provider = libagents::parse_provider_type(argv[1]);

    std::cout << "Using provider: " << libagents::provider_type_name(provider) << std::endl;

    AppContext ctx;

    auto agent = libagents::create_agent(provider);

    // This tool MUST run on main thread (accesses ctx)
    agent->register_tool(libagents::make_tool(
        "increment", "Increment the counter. Call this tool when asked to increment.",
        [&ctx]() {
            ctx.increment();  // Safe: runs on main thread via query_hosted
            return "Counter incremented to " + std::to_string(ctx.counter);
        },
        {}
    ));

    agent->set_system_prompt(
        "You have an 'increment' tool that increments a counter. "
        "When asked to increment, call the tool. Be concise.");

    if (!agent->initialize())
    {
        std::cerr << "Failed to initialize agent" << std::endl;
        return 1;
    }

    // HostContext: events + abort check
    libagents::HostContext host;
    host.on_event = [](const libagents::Event& ev) {
        if (ev.type == libagents::EventType::ContentDelta)
            std::cout << ev.content << std::flush;
        else if (ev.type == libagents::EventType::ToolCall)
            std::cout << "\n  [calling tool: " << ev.tool_name << "]\n";
    };

    std::cout << "\nQuery: Please increment the counter 3 times.\n" << std::endl;

    // query_hosted: AI runs on background thread, but tools dispatch to HERE
    agent->query_hosted(
        "Please increment the counter 3 times, calling the increment tool each time.",
        host
    );

    std::cout << "\n\nFinal counter value: " << ctx.counter << std::endl;
    return 0;
}
