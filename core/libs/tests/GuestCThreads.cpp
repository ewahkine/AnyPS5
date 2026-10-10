#include "prx/libc/include/general/VabiMacros.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <stdexcept>
#include <thread>

extern "C" {
int APS5_VABI _Mtx_init_nid_postfix(void**, int);
int APS5_VABI _Mtx_init_with_name_nid_postfix(void**, int, const char*);
void APS5_VABI _Mtx_destroy_nid_postfix(void**);
int APS5_VABI _Mtx_lock_nid_postfix(void**);
int APS5_VABI _Mtx_unlock_nid_postfix(void**);
int APS5_VABI _Cnd_init_nid_postfix(void**);
void APS5_VABI _Cnd_destroy_nid_postfix(void**);
int APS5_VABI _Cnd_broadcast_nid_postfix(void**);
int APS5_VABI _Cnd_signal_nid_postfix(void**);
int APS5_VABI _Cnd_wait_nid_postfix(void**, void**);
}

static void Require(bool value) { if (!value) std::abort(); }

template <typename TCall>
static void RequireThrows(TCall call) {
    try {
        call();
    } catch (const std::runtime_error&) {
        return;
    }
    std::abort();
}

constexpr int Plain = 0x01;
constexpr int Try = 0x02;
constexpr int Recursive = 0x100;

static void MutualExclusion() {
    void* mutex = nullptr;
    Require(_Mtx_init_nid_postfix(&mutex, Try) == 0 && mutex != nullptr);
    int counter = 0;
    std::atomic<int> inside{0};
    std::array<std::thread, 8> workers;
    for (auto& worker : workers) worker = std::thread([&] {
        for (int i = 0; i < 2000; ++i) {
            Require(_Mtx_lock_nid_postfix(&mutex) == 0);
            Require(++inside == 1);
            ++counter;
            Require(--inside == 0);
            Require(_Mtx_unlock_nid_postfix(&mutex) == 0);
        }
    });
    for (auto& worker : workers) worker.join();
    Require(counter == 16000);
    _Mtx_destroy_nid_postfix(&mutex);
    Require(mutex == nullptr);
}

static void RecursiveOwnership() {
    void* mutex = nullptr;
    Require(_Mtx_init_nid_postfix(&mutex, Plain | Recursive) == 0);
    Require(_Mtx_lock_nid_postfix(&mutex) == 0);
    Require(_Mtx_lock_nid_postfix(&mutex) == 0);
    std::atomic<bool> acquired{false};
    std::thread other([&] {
        Require(_Mtx_lock_nid_postfix(&mutex) == 0);
        acquired = true;
        Require(_Mtx_unlock_nid_postfix(&mutex) == 0);
    });
    Require(_Mtx_unlock_nid_postfix(&mutex) == 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    Require(!acquired);
    RequireThrows([&] { _Mtx_destroy_nid_postfix(&mutex); });
    Require(_Mtx_unlock_nid_postfix(&mutex) == 0);
    other.join();
    Require(acquired);
    _Mtx_destroy_nid_postfix(&mutex);
}

static void InvalidUse() {
    void* mutex = nullptr;
    RequireThrows([&] { _Mtx_init_nid_postfix(&mutex, 0x8); });
    Require(mutex == nullptr);
    RequireThrows([&] { _Mtx_lock_nid_postfix(&mutex); });
    Require(_Mtx_init_nid_postfix(&mutex, Plain) == 0);
    RequireThrows([&] { _Mtx_unlock_nid_postfix(&mutex); });
    Require(_Mtx_lock_nid_postfix(&mutex) == 0);
    RequireThrows([&] { _Mtx_lock_nid_postfix(&mutex); });
    std::thread([&] { RequireThrows([&] { _Mtx_unlock_nid_postfix(&mutex); }); }).join();
    void* cond = nullptr;
    RequireThrows([&] { _Cnd_wait_nid_postfix(&cond, &mutex); });
    Require(_Mtx_unlock_nid_postfix(&mutex) == 0);
    Require(_Cnd_init_nid_postfix(&cond) == 0);
    RequireThrows([&] { _Cnd_wait_nid_postfix(&cond, &mutex); });
    _Cnd_destroy_nid_postfix(&cond);
    _Mtx_destroy_nid_postfix(&mutex);
    _Mtx_destroy_nid_postfix(&mutex);
    _Cnd_destroy_nid_postfix(&cond);

    Require(_Mtx_init_nid_postfix(&mutex, Plain | Recursive) == 0);
    Require(_Cnd_init_nid_postfix(&cond) == 0);
    Require(_Mtx_lock_nid_postfix(&mutex) == 0);
    Require(_Mtx_lock_nid_postfix(&mutex) == 0);
    RequireThrows([&] { _Cnd_wait_nid_postfix(&cond, &mutex); });
    Require(_Mtx_unlock_nid_postfix(&mutex) == 0);
    Require(_Mtx_unlock_nid_postfix(&mutex) == 0);
    _Cnd_destroy_nid_postfix(&cond);
    _Mtx_destroy_nid_postfix(&mutex);
}

static void WaitAndBroadcast() {
    void* mutex = nullptr;
    void* cond = nullptr;
    Require(_Mtx_init_nid_postfix(&mutex, Plain) == 0);
    Require(_Cnd_init_nid_postfix(&cond) == 0 && cond != nullptr);
    int generation = 0;
    int woken = 0;
    std::atomic<int> waiting{0};
    std::array<std::thread, 6> waiters;
    for (auto& waiter : waiters) waiter = std::thread([&] {
        Require(_Mtx_lock_nid_postfix(&mutex) == 0);
        ++waiting;
        while (generation == 0) Require(_Cnd_wait_nid_postfix(&cond, &mutex) == 0);
        ++woken;
        Require(_Mtx_unlock_nid_postfix(&mutex) == 0);
    });
    while (waiting != 6) std::this_thread::yield();
    Require(_Mtx_lock_nid_postfix(&mutex) == 0);
    generation = 1;
    Require(_Cnd_broadcast_nid_postfix(&cond) == 0);
    Require(_Mtx_unlock_nid_postfix(&mutex) == 0);
    for (auto& waiter : waiters) waiter.join();
    Require(woken == 6);
    _Cnd_destroy_nid_postfix(&cond);
    Require(cond == nullptr);
    _Mtx_destroy_nid_postfix(&mutex);
}

static void Signal() {
    void* mutex = nullptr;
    void* cond = nullptr;
    Require(_Mtx_init_with_name_nid_postfix(&mutex, Plain, "condition") == 0 && mutex != nullptr);
    Require(_Cnd_init_nid_postfix(&cond) == 0);
    int ready = 0;
    bool waiting = false;
    std::thread waiter([&] {
        Require(_Mtx_lock_nid_postfix(&mutex) == 0);
        waiting = true;
        while (ready == 0) Require(_Cnd_wait_nid_postfix(&cond, &mutex) == 0);
        ready = 2;
        Require(_Mtx_unlock_nid_postfix(&mutex) == 0);
    });
    for (;;) {
        Require(_Mtx_lock_nid_postfix(&mutex) == 0);
        const bool started = waiting;
        if (started) {
            ready = 1;
            Require(_Cnd_signal_nid_postfix(&cond) == 0);
        }
        Require(_Mtx_unlock_nid_postfix(&mutex) == 0);
        if (started) break;
        std::this_thread::yield();
    }
    waiter.join();
    Require(ready == 2);
    _Cnd_destroy_nid_postfix(&cond);
    _Mtx_destroy_nid_postfix(&mutex);
}

int main() {
    MutualExclusion();
    RecursiveOwnership();
    InvalidUse();
    WaitAndBroadcast();
    Signal();
}
