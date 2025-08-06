#include "command_executor.h"
#include "utils.h"
#include <cstdlib>
#include <chrono>

// Global instance
CommandExecutor g_command_executor;

CommandExecutor::CommandExecutor() : should_stop(false) {
}

CommandExecutor::~CommandExecutor() {
    stop();
}

void CommandExecutor::start() {
    if (worker_thread.joinable()) {
        return; // Already started
    }
    
    should_stop = false;
    worker_thread = std::thread(&CommandExecutor::worker_loop, this);
    Logger::log(LogLevel::INFO, "CommandExecutor started");
}

void CommandExecutor::stop() {
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        should_stop = true;
    }
    queue_cv.notify_all();
    
    if (worker_thread.joinable()) {
        worker_thread.join();
    }
    Logger::log(LogLevel::INFO, "CommandExecutor stopped");
}

void CommandExecutor::worker_loop() {
    while (!should_stop) {
        Command cmd("", nullptr);
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            queue_cv.wait(lock, [this] { return !command_queue.empty() || should_stop; });
            
            if (should_stop) {
                break;
            }
            
            if (!command_queue.empty()) {
                cmd = command_queue.front();
                command_queue.pop();
            } else {
                continue;
            }
        }
        
        // Execute command outside of lock
        if (!cmd.cmd.empty()) {
            int exit_code = execute_sync(cmd.cmd);
            
            // Call callback if provided
            if (cmd.callback) {
                cmd.callback(exit_code);
            }
        }
    }
}

int CommandExecutor::execute_sync(const std::string& command) {
    Logger::log(LogLevel::DEBUG, "Executing: " + command);
    
    auto start = std::chrono::high_resolution_clock::now();
    int result = system(command.c_str());
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    Logger::log(LogLevel::DEBUG, "Command completed in " + std::to_string(duration.count()) + "ms, exit code: " + std::to_string(result));
    
    return result;
}

void CommandExecutor::execute_async(const std::string& command, std::function<void(int)> callback) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        command_queue.emplace(command, callback);
    }
    queue_cv.notify_one();
}

int CommandExecutor::execute_command(const std::string& command) {
    // For synchronous execution, just call directly
    return execute_sync(command);
}

bool CommandExecutor::execute_batch(const std::vector<std::string>& commands) {
    Logger::log(LogLevel::INFO, "Executing batch of " + std::to_string(commands.size()) + " commands");
    
    bool all_success = true;
    for (const auto& cmd : commands) {
        int result = execute_sync(cmd);
        if (result != 0) {
            Logger::log(LogLevel::WARNING, "Batch command failed: " + cmd + " (exit code: " + std::to_string(result) + ")");
            all_success = false;
        }
    }
    
    Logger::log(LogLevel::INFO, "Batch execution completed, success: " + std::string(all_success ? "true" : "false"));
    return all_success;
}

void CommandExecutor::wait_for_completion() {
    while (true) {
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            if (command_queue.empty()) {
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
