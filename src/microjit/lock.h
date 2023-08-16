//
// Created by cycastic on 8/14/23.
//

#ifndef MICROJIT_LOCK_H
#define MICROJIT_LOCK_H

#include <shared_mutex>
#include "def.h"

namespace microjit {
    class BaseRWLock {
    public:
        virtual void read_lock() = 0;
        virtual void read_unlock() = 0;
        virtual bool try_read_lock() = 0;
        virtual void write_lock() = 0;
        virtual void write_unlock() = 0;
        virtual bool try_write_lock() = 0;
    };

    class RWLock : public BaseRWLock {
        std::shared_timed_mutex mutex;
    public:
        void read_lock() override { mutex.lock_shared(); }
        void read_unlock() override { mutex.unlock_shared(); }
        bool try_read_lock() override { return mutex.try_lock_shared(); }
        void write_lock() override { mutex.lock(); }
        void write_unlock() override { mutex.unlock(); }
        bool try_write_lock() override { return mutex.try_lock(); }
    };

    class InertRWLock : public BaseRWLock {
    public:
        void read_lock() override {}
        void read_unlock() override {}
        bool try_read_lock() override { return true; }
        void write_lock() override {}
        void write_unlock() override {}
        bool try_write_lock() override { return true; }
    };

    class ReadLockGuard {
    private:
        BaseRWLock* lock;
    public:
        explicit ReadLockGuard(BaseRWLock& p_lock) : lock(&p_lock) { lock->read_lock(); }
        ~ReadLockGuard() { lock->read_unlock(); }
    };
    class WriteLockGuard {
    private:
        BaseRWLock* lock;
    public:
        explicit WriteLockGuard(BaseRWLock& p_lock) : lock(&p_lock) { lock->write_lock(); }
        ~WriteLockGuard() { lock->write_unlock(); }
    };
}


#endif //MICROJIT_LOCK_H
