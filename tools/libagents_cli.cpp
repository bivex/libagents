/// @file libagents_cli.cpp
/// @brief Command-line interface for libagentscpp
///
/// A diagnostic CLI tool for testing and exercising libagentscpp providers
/// without needing a debugger.
///
/// Usage:
///   libagents_cli [options] [query]
///
/// Options:
///   -p, --provider <name>   Provider: claude or copilot (default: claude)
///   -s, --system <prompt>   System prompt (or @file to load from file)
///   -i, --interactive       Interactive mode (REPL)
///   -t, --tool              Enable test tool (echo)
///   -v, --verbose           Verbose output
///   -h, --help              Show help
///
/// Examples:
///   libagents_cli "What is 2+2?"
///   libagents_cli -p copilot -i
///   libagents_cli -p claude -s "You are a helpful assistant" "Hello"
///   libagents_cli -p claude -s @system_prompt.txt "Help me"

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>

#include <libagents/agent.hpp>

namespace {

struct Options {
    std::string provider = "claude";
    std::string system_prompt;
    std::string query;
    bool interactive = false;
    bool enable_tool = false;
    bool verbose = false;
    bool help = false;
};

void print_usage(const char* program) {
    std::cout << R"(
libagents_cli - Command-line interface for libagentscpp

Usage: )" << program << R"( [options] [query]

Options:
  -p, --provider <name>   Provider: claude or copilot (default: claude)
  -s, --system <prompt>   System prompt (or @file to load from file)
  -i, --interactive       Interactive mode (REPL)
  -t, --tool              Enable test tool (echo)
  -v, --verbose           Verbose output
  -h, --help              Show help

Examples:
  )" << program << R"( "What is 2+2?"
  )" << program << R"( -p copilot -i
  )" << program << R"( -p claude -s "You are a helpful assistant" "Hello"
  )" << program << R"( -t "use the echo tool to repeat 'hello world'"

)";
}

std::string load_file(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        std::cerr << "Error: Cannot open file: " << path << "\n";
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

Options parse_args(int argc, char* argv[]) {
    Options opts;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            opts.help = true;
        } else if (arg == "-i" || arg == "--interactive") {
            opts.interactive = true;
        } else if (arg == "-t" || arg == "--tool") {
            opts.enable_tool = true;
        } else if (arg == "-v" || arg == "--verbose") {
            opts.verbose = true;
        } else if ((arg == "-p" || arg == "--provider") && i + 1 < argc) {
            opts.provider = argv[++i];
        } else if ((arg == "-s" || arg == "--system") && i + 1 < argc) {
            std::string val = argv[++i];
            if (!val.empty() && val[0] == '@') {
                opts.system_prompt = load_file(val.substr(1));
            } else {
                opts.system_prompt = val;
            }
        } else if (arg[0] != '-') {
            // Positional argument is the query
            opts.query = arg;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
        }
    }

    return opts;
}

void run_interactive(libagents::IAgent& agent, bool verbose) {
    std::cout << "\n[" << agent.provider_name() << "] Interactive mode. Type 'quit' to exit.\n\n";

    std::string line;
    while (true) {
        std::cout << "> ";
        std::cout.flush();

        if (!std::getline(std::cin, line)) {
            break;
        }

        // Trim whitespace
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        size_t end = line.find_last_not_of(" \t\r\n");
        line = line.substr(start, end - start + 1);

        if (line.empty()) continue;
        if (line == "quit" || line == "exit" || line == "q") break;
        if (line == "clear") {
            agent.clear_session();
            std::cout << "[Session cleared]\n\n";
            continue;
        }

        if (verbose) {
            std::cout << "[Querying...]\n";
        }

        try {
            // Use streaming for interactive mode
            std::cout << "\n";
            std::string response = agent.query_streaming(line,
                [](const libagents::Event& event) {
                    if (event.type == libagents::EventType::ContentDelta) {
                        std::cout << event.content;
                        std::cout.flush();
                    } else if (event.type == libagents::EventType::ToolCall) {
                        std::cout << "\n[Tool: " << event.tool_name << "]\n";
                    } else if (event.type == libagents::EventType::Error) {
                        std::cerr << "\n[Error: " << event.content << "]\n";
                    }
                });
            std::cout << "\n\n";
        } catch (const std::exception& e) {
            std::cerr << "\nError: " << e.what() << "\n\n";
        }
    }

    std::cout << "\nGoodbye!\n";
}

void run_single_query(libagents::IAgent& agent, const std::string& query, bool verbose) {
    if (verbose) {
        std::cout << "[Provider: " << agent.provider_name() << "]\n";
        std::cout << "[Query: " << query << "]\n";
        std::cout << "[Querying...]\n\n";
    }

    try {
        bool had_streaming_content = false;
        std::string response = agent.query_streaming(query,
            [&had_streaming_content](const libagents::Event& event) {
                if (event.type == libagents::EventType::ContentDelta) {
                    std::cout << event.content;
                    std::cout.flush();
                    had_streaming_content = true;
                } else if (event.type == libagents::EventType::ToolCall) {
                    std::cout << "\n[Tool: " << event.tool_name << "]\n";
                }
            });
        // Print response if no streaming content was received
        if (!had_streaming_content && !response.empty()) {
            std::cout << response;
        }
        std::cout << "\n";
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
    }
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    Options opts = parse_args(argc, argv);

    if (opts.help) {
        print_usage(argv[0]);
        return 0;
    }

    // Need either interactive mode or a query
    if (!opts.interactive && opts.query.empty()) {
        print_usage(argv[0]);
        return 1;
    }

    // Parse provider type
    libagents::ProviderType provider_type = libagents::parse_provider_type(opts.provider);

    if (opts.verbose) {
        std::cout << "[Creating agent with provider: " << opts.provider << "]\n";
    }

    // Create agent
    auto agent = libagents::create_agent(provider_type);
    if (!agent) {
        std::cerr << "Error: Failed to create agent for provider: " << opts.provider << "\n";
        return 1;
    }

    // Set system prompt if provided
    if (!opts.system_prompt.empty()) {
        agent->set_system_prompt(opts.system_prompt);
        if (opts.verbose) {
            std::cout << "[System prompt set (" << opts.system_prompt.length() << " chars)]\n";
        }
    }

    // Register test tool if requested
    if (opts.enable_tool) {
        libagents::Tool echo_tool;
        echo_tool.name = "echo";
        echo_tool.description = "Echo back the provided message. Use this to test tool execution.";
        echo_tool.parameters_schema = R"({
            "type": "object",
            "properties": {
                "message": {
                    "type": "string",
                    "description": "The message to echo back"
                }
            },
            "required": ["message"]
        })";
        echo_tool.handler = [](const std::string& args) -> std::string {
            // Simple JSON parsing for the message field
            // In production you'd use a proper JSON library
            size_t pos = args.find("\"message\"");
            if (pos != std::string::npos) {
                pos = args.find(":", pos);
                if (pos != std::string::npos) {
                    pos = args.find("\"", pos);
                    if (pos != std::string::npos) {
                        size_t end = args.find("\"", pos + 1);
                        if (end != std::string::npos) {
                            return "ECHO: " + args.substr(pos + 1, end - pos - 1);
                        }
                    }
                }
            }
            return "ECHO: " + args;
        };

        agent->register_tool(echo_tool);

        if (opts.verbose) {
            std::cout << "[Registered tool: echo]\n";
        }
    }

    // Initialize
    if (opts.verbose) {
        std::cout << "[Initializing...]\n";
    }

    if (!agent->initialize()) {
        std::cerr << "Error: Failed to initialize agent\n";
        return 1;
    }

    if (opts.verbose) {
        std::cout << "[Initialized successfully]\n";
    }

    // Run
    if (opts.interactive) {
        run_interactive(*agent, opts.verbose);
    } else {
        run_single_query(*agent, opts.query, opts.verbose);
    }

    // Cleanup
    agent->shutdown();

    return 0;
}
