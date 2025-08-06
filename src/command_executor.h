#ifndef COMMAND_EXECUTOR_H
#define COMMAND_EXECUTOR_H

#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <functional>

class CommandExecutor {
public:
    struct Command {
        std::string cmd;
        std::function<void(int)> callback;  // callback with exit code
        
        Command(const std::string& command, std::function<void(int)> cb = nullptr)
            : cmd(command), callback(cb) {}
    };

private:
    std::queue<Command> command_queue;
    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    std::thread worker_thread;
    std::atomic<bool> should_stop;
    
    void worker_loop();
    int execute_sync(const std::string& command);

public:
    CommandExecutor();
    ~CommandExecutor();
    
    // Start the background worker thread
    void start();
    
    // Stop the background worker thread
    void stop();
    
    // Add command to queue (asynchronous)
    void execute_async(const std::string& command, std::function<void(int)> callback = nullptr);
    
    // Execute command synchronously (blocks until done)
    int execute_command(const std::string& command);
    
    // Execute multiple commands synchronously with batching
    bool execute_batch(const std::vector<std::string>& commands);
    
    // Wait for all queued commands to complete
    void wait_for_completion();
    
    // Check if executor is running
    bool is_running() const { return !should_stop && worker_thread.joinable(); }
};

// Global command executor instance
extern CommandExecutor g_command_executor;

#endif // COMMAND_EXECUTOR_H
