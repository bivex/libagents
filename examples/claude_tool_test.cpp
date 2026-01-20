/// @file claude_tool_test.cpp
/// @brief Test MCP tools via both SDK directly and through libagents
///
/// This example tests MCP tool registration to isolate whether issues
/// are in the Claude Code SDK or in the libagents wrapper.

#include <iostream>
#include <string>

// SDK headers (direct access)
#include <claude/claude.hpp>

// libagents headers (wrapper)
#include <libagents/agent.hpp>
#include <libagents/tool_builder.hpp>

/// Test 1: Direct SDK usage (like mcp_calculator example)
bool test_sdk_direct()
{
    std::cout << "\n=== TEST 1: Direct Claude SDK ===\n" << std::endl;

    try
    {
        // Create a simple single-argument tool
        auto simple_tool = claude::mcp::make_tool(
            "sdk_echo",
            "Echo a message back (SDK test)",
            [](std::string message) -> std::string {
                std::cout << "[SDK Tool called: " << message << "]" << std::endl;
                return "SDK echoed: " + message;
            },
            std::vector<std::string>{"message"});

        // Create a complex 6-argument tool
        auto complex_tool = claude::mcp::make_tool(
            "sdk_compute",
            "Compute with 6 arguments: base, offset, multiplier, divisor, add_value, format",
            [](double base, double offset, double multiplier, double divisor,
               double add_value, std::string format) -> std::string {
                std::cout << "[SDK Complex Tool: base=" << base << " offset=" << offset
                          << " mult=" << multiplier << " div=" << divisor
                          << " add=" << add_value << " fmt=" << format << "]" << std::endl;
                if (divisor == 0.0) return "Error: division by zero";
                double result = ((base + offset) * multiplier / divisor) + add_value;
                if (format == "int") return std::to_string(static_cast<int>(result));
                return std::to_string(result);
            },
            std::vector<std::string>{"base", "offset", "multiplier", "divisor", "add_value", "format"});

        // Create MCP server with both tools
        auto server = claude::mcp::create_server("test", "1.0.0",
            std::move(simple_tool), std::move(complex_tool));

        // Configure Claude
        claude::ClaudeOptions options;
        options.permission_mode = "bypassPermissions";
        options.sdk_mcp_handlers["test"] = server;
        options.allowed_tools = {"mcp__test__sdk_echo", "mcp__test__sdk_compute"};

        std::cout << "SDK: Created MCP server with 2 tools (simple + 6-arg complex)" << std::endl;
        std::cout << "SDK: Allowed tools: mcp__test__sdk_echo, mcp__test__sdk_compute" << std::endl;

        // Connect and query
        claude::ClaudeClient client(options);
        client.connect();
        std::cout << "SDK: Connected to Claude" << std::endl;

        client.send_query("What tools do you have? List them all.");
        std::cout << "SDK: Sent query" << std::endl;

        std::string response;
        for (const auto& msg : client.receive_messages())
        {
            if (claude::is_assistant_message(msg))
            {
                const auto& assistant = std::get<claude::AssistantMessage>(msg);
                for (const auto& block : assistant.content)
                {
                    if (std::holds_alternative<claude::TextBlock>(block))
                    {
                        response += std::get<claude::TextBlock>(block).text;
                    }
                }
            }
            else if (claude::is_result_message(msg))
            {
                break;
            }
        }

        client.disconnect();

        std::cout << "\nSDK Response:\n" << response << std::endl;

        // Check if our tools are visible
        bool has_simple = response.find("sdk_echo") != std::string::npos ||
                          response.find("mcp__test__sdk_echo") != std::string::npos;
        bool has_complex = response.find("sdk_compute") != std::string::npos ||
                           response.find("mcp__test__sdk_compute") != std::string::npos;
        std::cout << "\nSDK: Simple tool visible? " << (has_simple ? "YES" : "NO") << std::endl;
        std::cout << "SDK: Complex 6-arg tool visible? " << (has_complex ? "YES" : "NO") << std::endl;

        return has_simple && has_complex;
    }
    catch (const std::exception& e)
    {
        std::cerr << "SDK ERROR: " << e.what() << std::endl;
        return false;
    }
}

/// Test 2: Through libagents wrapper
bool test_libagents_wrapper()
{
    std::cout << "\n=== TEST 2: libagents Wrapper ===\n" << std::endl;

    try
    {
        // Create agent
        auto agent = libagents::create_agent(libagents::ProviderType::Claude);
        if (!agent)
        {
            std::cerr << "libagents: Failed to create agent" << std::endl;
            return false;
        }
        std::cout << "libagents: Created agent: " << agent->provider_name() << std::endl;

        // Register simple tool
        auto simple_tool = libagents::make_tool(
            "libagents_echo",
            "Echo a message back (libagents test)",
            [](std::string message) -> std::string {
                std::cout << "[libagents Tool called: " << message << "]" << std::endl;
                return "libagents echoed: " + message;
            },
            {"message"});
        agent->register_tool(simple_tool);

        // Register complex 6-argument tool
        auto complex_tool = libagents::make_tool(
            "libagents_compute",
            "Compute with 6 arguments: base, offset, multiplier, divisor, add_value, format",
            [](double base, double offset, double multiplier, double divisor,
               double add_value, std::string format) -> std::string {
                std::cout << "[libagents Complex Tool: base=" << base << " offset=" << offset
                          << " mult=" << multiplier << " div=" << divisor
                          << " add=" << add_value << " fmt=" << format << "]" << std::endl;
                if (divisor == 0.0) return "Error: division by zero";
                double result = ((base + offset) * multiplier / divisor) + add_value;
                if (format == "int") return std::to_string(static_cast<int>(result));
                return std::to_string(result);
            },
            {"base", "offset", "multiplier", "divisor", "add_value", "format"});
        agent->register_tool(complex_tool);

        std::cout << "libagents: Registered 2 tools (simple + 6-arg complex)" << std::endl;

        // Set system prompt
        agent->set_system_prompt("You are a test assistant.");
        std::cout << "libagents: System prompt set" << std::endl;

        // Initialize
        if (!agent->initialize())
        {
            std::cerr << "libagents: Failed to initialize" << std::endl;
            return false;
        }
        std::cout << "libagents: Initialized" << std::endl;

        // Query
        std::cout << "libagents: Sending query..." << std::endl;

        std::string response = agent->query_streaming(
            "What tools do you have? List them all.",
            [](const libagents::Event& event) {
                if (event.type == libagents::EventType::ContentDelta)
                {
                    // Print streaming content
                }
            });

        std::cout << "\nlibagents Response:\n" << response << std::endl;

        // Check if our tools are visible
        bool has_simple = response.find("libagents_echo") != std::string::npos ||
                          response.find("mcp__tools__libagents_echo") != std::string::npos;
        bool has_complex = response.find("libagents_compute") != std::string::npos ||
                           response.find("mcp__tools__libagents_compute") != std::string::npos;
        std::cout << "\nlibagents: Simple tool visible? " << (has_simple ? "YES" : "NO") << std::endl;
        std::cout << "libagents: Complex 6-arg tool visible? " << (has_complex ? "YES" : "NO") << std::endl;

        agent->shutdown();
        return has_simple && has_complex;
    }
    catch (const std::exception& e)
    {
        std::cerr << "libagents ERROR: " << e.what() << std::endl;
        return false;
    }
}

int main()
{
    std::cout << "============================================" << std::endl;
    std::cout << "  Claude MCP Tool Visibility Test" << std::endl;
    std::cout << "============================================" << std::endl;

    bool sdk_ok = test_sdk_direct();
    bool libagents_ok = test_libagents_wrapper();

    std::cout << "\n============================================" << std::endl;
    std::cout << "  RESULTS:" << std::endl;
    std::cout << "============================================" << std::endl;
    std::cout << "Direct SDK:       " << (sdk_ok ? "PASS (tool visible)" : "FAIL (tool NOT visible)") << std::endl;
    std::cout << "libagents wrapper: " << (libagents_ok ? "PASS (tool visible)" : "FAIL (tool NOT visible)") << std::endl;
    std::cout << "============================================" << std::endl;

    if (sdk_ok && !libagents_ok)
    {
        std::cout << "\nDiagnosis: Issue is in libagents wrapper" << std::endl;
    }
    else if (!sdk_ok && !libagents_ok)
    {
        std::cout << "\nDiagnosis: Issue is in Claude SDK or environment" << std::endl;
    }
    else if (sdk_ok && libagents_ok)
    {
        std::cout << "\nDiagnosis: Both working - issue is specific to WinDbg integration" << std::endl;
    }

    return (sdk_ok && libagents_ok) ? 0 : 1;
}
