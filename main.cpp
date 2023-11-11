#include <iostream>
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <sys/types.h>
#include <mach/mach.h>
#include <unistd.h>

#include <lldb/API/LLDB.h>

int fd[2];

struct ProcessData {
    vm_address_t address = 0;
    vm_size_t size = 0;
    vm_offset_t data = 0;
    mach_msg_type_number_t dataCnt = 0;
};

void lldb_stuff(const std::string& path, lldb::pid_t pid, ProcessData& data) {
    lldb::SBDebugger::Initialize();
    lldb::SBDebugger debugger = lldb::SBDebugger::Create();

    lldb::SBTarget target = debugger.CreateTargetWithFileAndArch(path.c_str(), nullptr);

    lldb::SBListener listener;
    lldb::SBError error;    
    lldb::SBProcess process = target.AttachToProcessWithID(listener, pid, error);

    // Check if the process is successfully attached
    if (!process.IsValid() || error.Fail()) {
        std::cerr << "Failed to attach to process: " << error.GetCString() << std::endl;
        // Handle error
    }

    // Reading memory
    lldb::addr_t address = data.address;
    size_t size = data.size;
    char buffer[size];
    process.ReadMemory(address, buffer, size, error);

    // Check for read error
    if (error.Fail()) {
        std::cerr << "Memory read error: " << error.GetCString() << std::endl;
        // Handle error
    }
}

pid_t launch(const std::string& path) {
    if (pipe(fd) == -1) {
        std::cerr << "Pipe failed" << std::endl;
        return -1;
    }

    pid_t pid = fork();

    if (pid == -1) {
        return -1;
    } else if (pid > 0) {
        // Parent process
        close(fd[1]); // Close unused write end
        
        std::cout << "Started process with PID: " << pid << std::endl;
        return pid;
    } else {
        // Child process
        close(fd[0]); // Close unused read end
        dup2(fd[1], STDOUT_FILENO); // Redirect stdout to the pipe
        close(fd[1]);

        execl(path.c_str(), "tracee-program", (char *)NULL);

        // execl only returns if there is an error
        std::cerr << "Failed to start " << path << std::endl;
        return -2;
    }
}

mach_port_t request_task(pid_t pid) {
    mach_port_t port;
    kern_return_t kr = task_for_pid(mach_task_self(), pid, &port);
    if (kr != KERN_SUCCESS) {
        throw std::runtime_error("Could not request a task");
    }

    std::cout << "task port: " << port << std::endl;

    return port;
}

void read(mach_port_t port, ProcessData& data) {
    std::cout << "Attempting to read " << data.address << " from port " << port << std::endl;
    kern_return_t kr = vm_read(port, data.address, data.size, &data.data, &data.dataCnt);
    if (kr != KERN_SUCCESS) {
        std::cout << "read error! " << mach_error_string(kr) << std::endl;
        // Handle error
    }
}

void write(mach_port_t port, const ProcessData& data) {
    kern_return_t kr = vm_write(port, data.address, data.data, data.dataCnt);
    if (kr != KERN_SUCCESS) {
        std::cout << "write error! " << mach_error_string(kr) << std::endl;
        // Handle error
    }
}

ProcessData receive_tracee_data() {
    ProcessData data;

    char buffer[128];
    ssize_t count;
    std::string receivedData;
    while ((count = read(fd[0], buffer, sizeof(buffer) - 1)) > 0) {
        buffer[count] = '\0';
        receivedData += buffer;
        break;
    }

    std::cout << "Received from tracee: " << receivedData << std::endl;

    std::istringstream iss(receivedData);
    std::string addressStr, typeInfo;
    if (iss >> addressStr >> typeInfo) {
        // Convert address string to vm_address_t
        std::istringstream addressStream(addressStr);
        addressStream >> std::hex >> data.address;

        // Determine the size based on typeInfo
        if (typeInfo == "b") {  // Boolean type
            data.size = sizeof(bool);
        }
        // Add more type checks if necessary
    }

    return data;
}

int main(int argc, char** argv) {

    std::string executable_path = argv[1];
    auto pid = launch(executable_path);
    //auto port = request_task(pid);

    while (true) {
        auto data = receive_tracee_data();
        if (data.address) {
            lldb_stuff(executable_path, pid, data);
            //read(port, data);
            //write(port, data);
        }
    }
}
