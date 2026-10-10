#include "prx/libc/include/CxxThreads.hpp"
#include "prx/libc/include/General.hpp"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

enum ThreadResult : int {
    ThreadSuccess = 0,
    ThreadNomem = 1,
    ThreadError = 4,
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

struct PadState {
    std::mutex mutex;
    std::condition_variable released;
    bool started = false;
};

struct Pad {
    unsigned (APS5_VABI * const* vtable)(Pad*);
    PadState* state;
};

std::atomic<const CxxThreadApi*> threadApi{nullptr};

const CxxThreadApi& ThreadApi(const char* function) {
    const auto* api = threadApi.load(std::memory_order_acquire);
    if (api == nullptr) throw std::runtime_error(std::string(function) + ": libkernel thread API is not registered");
    return *api;
}

PadState* PadStateFrom(Pad* pad, const char* function) {
    if (pad == nullptr || pad->state == nullptr)
        throw std::runtime_error(std::string(function) + ": thread launch pad is not constructed");
    return pad->state;
}

void* APS5_VABI RunPad(void* opaque) {
    auto* pad = static_cast<Pad*>(opaque);
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(pad->vtable[0](pad)));
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

int APS5_VABI _Mtx_init_with_name_nid_postfix(GuestMutex** slot, int type, const char*) {
    return _Mtx_init_nid_postfix(slot, type);
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
    std::lock_guard lock(mutex->guard);
    RequireOwner(*mutex, "_Mtx_unlock");
    if (--mutex->count == 0) {
        mutex->owner = {};
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

int APS5_VABI _Cnd_signal_nid_postfix(GuestCond** slot) {
    CondFrom(slot, "_Cnd_signal")->waiters.notify_one();
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

void APS5_VABI _ZNSt4_PadC2Ev_nid_postfix(Pad* pad) {
    if (pad == nullptr) throw std::runtime_error("std::_Pad::_Pad: null pad");
    pad->state = new PadState;
}

void APS5_VABI _ZNSt4_PadD2Ev_nid_postfix(Pad* pad) {
    if (pad == nullptr) return;
    delete pad->state;
    pad->state = nullptr;
}

void APS5_VABI _ZNSt4_Pad7_LaunchEPP7pthread_nid_postfix(Pad* pad, Pthread* thread) {
    auto* state = PadStateFrom(pad, "std::_Pad::_Launch");
    if (thread == nullptr) throw std::runtime_error("std::_Pad::_Launch: null thread");
    const auto& api = ThreadApi("std::_Pad::_Launch");
    if (const int result = api.create(thread, nullptr, RunPad, pad, nullptr); result != 0)
        throw std::runtime_error("std::_Pad::_Launch: thread creation failed with " + std::to_string(result));
    std::unique_lock lock(state->mutex);
    state->released.wait(lock, [state] { return state->started; });
}

void APS5_VABI _ZNSt4_Pad8_ReleaseEv_nid_postfix(Pad* pad) {
    auto* state = PadStateFrom(pad, "std::_Pad::_Release");
    std::lock_guard lock(state->mutex);
    state->started = true;
    state->released.notify_one();
}

Pthread APS5_VABI _Thrd_id_nid_postfix() {
    return ThreadApi("_Thrd_id").self();
}

int APS5_VABI _Thrd_join_nid_postfix(Pthread thread, int* result) {
    void* value = nullptr;
    if (ThreadApi("_Thrd_join").join(thread, &value) != 0) return ThreadError;
    if (result != nullptr) *result = static_cast<int>(reinterpret_cast<std::uintptr_t>(value));
    return ThreadSuccess;
}

}

void CxxThreadApiRegister_nid_no_patch(const CxxThreadApi* api) {
    if (api == nullptr || api->create == nullptr || api->join == nullptr || api->self == nullptr)
        throw std::invalid_argument("libc: incomplete thread API");
    static const CxxThreadApi registered = *api;
    if (registered.create != api->create || registered.join != api->join || registered.self != api->self)
        throw std::runtime_error("libc: cannot replace the registered thread API");
    threadApi.store(&registered, std::memory_order_release);
}
