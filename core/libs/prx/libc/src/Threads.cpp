#include "prx/libc/include/General.hpp"
#include <condition_variable>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

enum ThreadResult : int {
    ThreadSuccess = 0,
    ThreadNomem = 1,
};

enum MutexType : int {
    MutexPlain = 0x01,
    MutexTry = 0x02,
    MutexTimed = 0x04,
    MutexRecursive = 0x100,
};

struct GuestMutex {
    int type;
    std::mutex guard;
    std::condition_variable released;
    std::thread::id owner;
    unsigned long long count = 0;
};

struct GuestCond {
    std::condition_variable_any waiters;
};

GuestMutex* MutexFrom(GuestMutex** slot, const char* function) {
    if (slot == nullptr || *slot == nullptr)
        throw std::runtime_error(std::string(function) + ": mutex is not initialized");
    return *slot;
}

GuestCond* CondFrom(GuestCond** slot, const char* function) {
    if (slot == nullptr || *slot == nullptr)
        throw std::runtime_error(std::string(function) + ": condition variable is not initialized");
    return *slot;
}

void RequireOwner(const GuestMutex& mutex, const char* function) {
    if (mutex.count == 0 || mutex.owner != std::this_thread::get_id())
        throw std::runtime_error(std::string(function) + ": mutex is not owned by the calling thread");
}

}

extern "C" {

int APS5_VABI _Mtx_init_nid_postfix(GuestMutex** slot, int type) {
    if (slot == nullptr) throw std::runtime_error("_Mtx_init: null mutex");
    if ((type & ~(MutexPlain | MutexTry | MutexTimed | MutexRecursive)) != 0)
        throw std::runtime_error("_Mtx_init: unknown mutex type " + std::to_string(type));
    *slot = new (std::nothrow) GuestMutex{type};
    return *slot == nullptr ? ThreadNomem : ThreadSuccess;
}

void APS5_VABI _Mtx_destroy_nid_postfix(GuestMutex** slot) {
    if (slot == nullptr || *slot == nullptr) return;
    {
        std::lock_guard lock((*slot)->guard);
        if ((*slot)->count != 0) throw std::runtime_error("_Mtx_destroy: mutex is locked");
    }
    delete *slot;
    *slot = nullptr;
}

int APS5_VABI _Mtx_lock_nid_postfix(GuestMutex** slot) {
    auto* mutex = MutexFrom(slot, "_Mtx_lock");
    const auto self = std::this_thread::get_id();
    std::unique_lock lock(mutex->guard);
    if (mutex->count != 0 && mutex->owner == self) {
        if ((mutex->type & MutexRecursive) == 0)
            throw std::runtime_error("_Mtx_lock: non-recursive mutex is already owned by the calling thread");
        ++mutex->count;
        return ThreadSuccess;
    }
    mutex->released.wait(lock, [mutex] { return mutex->count == 0; });
    mutex->owner = self;
    mutex->count = 1;
    return ThreadSuccess;
}

int APS5_VABI _Mtx_unlock_nid_postfix(GuestMutex** slot) {
    auto* mutex = MutexFrom(slot, "_Mtx_unlock");
    std::unique_lock lock(mutex->guard);
    RequireOwner(*mutex, "_Mtx_unlock");
    if (--mutex->count == 0) {
        mutex->owner = {};
        lock.unlock();
        mutex->released.notify_one();
    }
    return ThreadSuccess;
}

int APS5_VABI _Cnd_init_nid_postfix(GuestCond** slot) {
    if (slot == nullptr) throw std::runtime_error("_Cnd_init: null condition variable");
    *slot = new (std::nothrow) GuestCond;
    return *slot == nullptr ? ThreadNomem : ThreadSuccess;
}

void APS5_VABI _Cnd_destroy_nid_postfix(GuestCond** slot) {
    if (slot == nullptr || *slot == nullptr) return;
    delete *slot;
    *slot = nullptr;
}

int APS5_VABI _Cnd_broadcast_nid_postfix(GuestCond** slot) {
    CondFrom(slot, "_Cnd_broadcast")->waiters.notify_all();
    return ThreadSuccess;
}

int APS5_VABI _Cnd_wait_nid_postfix(GuestCond** condSlot, GuestMutex** mutexSlot) {
    auto* cond = CondFrom(condSlot, "_Cnd_wait");
    auto* mutex = MutexFrom(mutexSlot, "_Cnd_wait");
    const auto self = std::this_thread::get_id();
    std::unique_lock lock(mutex->guard);
    RequireOwner(*mutex, "_Cnd_wait");
    if (mutex->count != 1)
        throw std::runtime_error("_Cnd_wait: recursive mutex is locked more than once");
    mutex->owner = {};
    mutex->count = 0;
    mutex->released.notify_one();
    cond->waiters.wait(lock);
    mutex->released.wait(lock, [mutex] { return mutex->count == 0; });
    mutex->owner = self;
    mutex->count = 1;
    return ThreadSuccess;
}

}
