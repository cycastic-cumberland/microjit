//
// Created by cycastic on 8/21/23.
//

#include "managed_thread.h"

ManagedThread::ID ManagedThread::main_thread_id = thread_id_hash(std::this_thread::get_id());
static thread_local ManagedThread::ID caller_id = 0;
static thread_local bool caller_id_cached = false;

uint64_t ManagedThread::thread_id_hash(const std::thread::id &p_native_id) {
    static std::hash<std::thread::id> hasher{};
    return hasher(p_native_id);
}

bool ManagedThread::is_started() const {
    return id != thread_id_hash(std::thread::id());
}

bool ManagedThread::is_alive() const {
    return !thread.joinable();
}

bool ManagedThread::is_finished() const {
    return !is_alive();
}

void ManagedThread::join() {
    if (id != thread_id_hash(std::thread::id())) {
        if (id == ManagedThread::this_thread_id()){
            // Can't join itself
            return;
        }
        thread.join();
        std::thread empty_thread;
        thread.swap(empty_thread);
        id = thread_id_hash(std::thread::id());
    }
}

ManagedThread::ID ManagedThread::this_thread_id() {
    if (likely(caller_id_cached)) {
        return caller_id;
    } else {
        caller_id = thread_id_hash(std::this_thread::get_id());
        caller_id_cached = true;
        return caller_id;
    }
}


ManagedThread::~ManagedThread(){
    if (id != thread_id_hash(std::thread::id())){
        thread.detach();
    }
}
