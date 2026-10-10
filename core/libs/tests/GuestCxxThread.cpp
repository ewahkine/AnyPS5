#include "SceTypes.hpp"
#include <array>
#include <atomic>
#include <cstdlib>
#include <thread>

struct GamePad;
using Go = unsigned (APS5_VABI *)(GamePad*);

struct Payload {
    Pthread self = nullptr;
    int value = 0;
    std::atomic<bool>* gate = nullptr;
};

struct GamePad {
    const Go* vtable;
    void* base[3];
    Payload* payload;
};

extern "C" {
void APS5_VABI _ZNSt4_PadC2Ev_nid_postfix(GamePad*);
void APS5_VABI _ZNSt4_PadD2Ev_nid_postfix(GamePad*);
void APS5_VABI _ZNSt4_Pad7_LaunchEPP7pthread_nid_postfix(GamePad*, Pthread*);
void APS5_VABI _ZNSt4_Pad8_ReleaseEv_nid_postfix(GamePad*);
Pthread APS5_VABI _Thrd_id_nid_postfix();
int APS5_VABI _Thrd_join_nid_postfix(Pthread, int*);
Pthread APS5_VABI scePthreadSelf();
}

static void Require(bool value) { if (!value) std::abort(); }

static unsigned APS5_VABI Run(GamePad* pad) {
    Payload* payload = pad->payload;
    pad->payload = nullptr;
    _ZNSt4_Pad8_ReleaseEv_nid_postfix(pad);
    if (payload->gate != nullptr)
        while (!payload->gate->load()) std::this_thread::yield();
    payload->self = _Thrd_id_nid_postfix();
    Require(payload->self == scePthreadSelf());
    payload->value = 42;
    return 7;
}

static const Go vtable[] = {Run};

static Pthread Launch(Payload& payload) {
    GamePad pad{};
    _ZNSt4_PadC2Ev_nid_postfix(&pad);
    pad.vtable = vtable;
    pad.payload = &payload;
    Pthread thread = nullptr;
    _ZNSt4_Pad7_LaunchEPP7pthread_nid_postfix(&pad, &thread);
    Require(pad.payload == nullptr);
    _ZNSt4_PadD2Ev_nid_postfix(&pad);
    return thread;
}

int main() {
    const Pthread main = _Thrd_id_nid_postfix();
    Require(main != nullptr && main == _Thrd_id_nid_postfix() && main == scePthreadSelf());

    std::atomic<bool> gate{false};
    Payload first;
    first.gate = &gate;
    const Pthread thread = Launch(first);
    Require(thread != nullptr && thread != main && first.value == 0);
    gate = true;
    int result = 0;
    Require(_Thrd_join_nid_postfix(thread, &result) == 0);
    Require(result == 7 && first.value == 42 && first.self == thread);

    std::array<Payload, 8> payloads;
    std::array<Pthread, 8> threads{};
    for (std::size_t index = 0; index < threads.size(); ++index) threads[index] = Launch(payloads[index]);
    for (std::size_t index = 0; index < threads.size(); ++index) {
        Require(_Thrd_join_nid_postfix(threads[index], nullptr) == 0);
        Require(payloads[index].value == 42 && payloads[index].self == threads[index]);
    }
}
