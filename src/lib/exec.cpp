#include "exec.hpp"

#include <unistd.h>
#include <sys/wait.h>
#include <csignal>
#include <thread>
#include <utility>
#include <iostream>

using namespace Csdr;

template <typename T, typename U>
ExecModule<T, U>::ExecModule(std::vector<std::string> args):
    Module<T, U>(),
    args(std::move(args))
{}

template <typename T, typename U>
ExecModule<T, U>::~ExecModule<T, U>() {
    run = false;
    if (child_pid != 0) {
        kill(child_pid, SIGTERM);
        waitpid(child_pid, NULL, 0);
    }
    if (readThread != nullptr) {
        readThread->join();
        delete readThread;
        readThread = nullptr;
    }
}

template <typename T, typename U>
void ExecModule<T, U>::startChild() {
    size_t s = args.size();
    const char* file = args[0].c_str();
    char* c_args[s];
    for (size_t i = 0; i < s; i++) {
        c_args[i] = (char*) args[i].c_str();
    }
    c_args[s] = NULL;

    int readPipe[2];
    pipe(readPipe);
    int writePipe[2];
    pipe(writePipe);

    child_pid = fork();
    int r;
    switch (child_pid) {
        case -1:
            throw std::runtime_error("could not fork");
        case 0:
            close(readPipe[0]);
            dup2(readPipe[1], STDOUT_FILENO);
            close(readPipe[1]);

            close(writePipe[1]);
            dup2(writePipe[0], STDIN_FILENO);
            close(writePipe[0]);

            r = execvp(file, c_args);
            if (r != 0) {
                throw std::runtime_error("could not exec");
            }
            break;
        default:
            // we are the parent, and pid is the child PID
            close(readPipe[1]);
            this->readPipe = readPipe[0];
            close(writePipe[0]);
            this->writePipe = writePipe[1];
            readThread = new std::thread([this] { readLoop(); });
            break;
    }
}

template <typename T, typename U>
void ExecModule<T, U>::readLoop() {
    size_t length;
    while(run && (length = read(this->readPipe, this->writer->getWritePointer(), 1024)) != 0) {
        std::cerr << "read " << length << " bytes from child\n";
        this->writer->advance(length / sizeof(U));
    }
    std::cerr << "read loop ending\n";
    close(this->readPipe);
}

template <typename T, typename U>
void ExecModule<T, U>::setWriter(Writer<U> *writer) {
    Module<T, U>::setWriter(writer);
    if (writer != nullptr) startChild();
}

template <typename T, typename U>
bool ExecModule<T, U>::canProcess() {
    return this->reader->available() > 0;
}

template <typename T, typename U>
void ExecModule<T, U>::process() {
    size_t size = std::min(this->reader->available(), (size_t) 1024);
    write(this->writePipe, this->reader->getReadPointer(), size * sizeof(T));
    this->reader->advance(size);
}

namespace Csdr {
    template class ExecModule<short, short>;
}