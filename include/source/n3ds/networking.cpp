#include "n3ds/networking.hpp"

#include "platform.hpp"

#include "LinkWebRTC.hpp"

#include <3ds.h>

#include <atomic>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

namespace n3ds::networking {

namespace {

class NetworkingThread {
    Thread m_thread{};

    LightEvent in_wakeup{};
    std::atomic<bool> in_run{};
    std::atomic<bool> in_waiting_for_completion{};

    LightEvent out_completed{};

    void net_thread();
    static void net_thread_start(void* ctx) {
        reinterpret_cast<NetworkingThread*>(ctx)->net_thread();
    }

    NetworkingThread() = default;
    ~NetworkingThread() = default;

    NetworkingThread(const NetworkingThread&) = delete;
    NetworkingThread(NetworkingThread&&) = delete;
    NetworkingThread& operator=(const NetworkingThread&) = delete;
    NetworkingThread& operator=(NetworkingThread&&) = delete;

    static std::atomic<NetworkingThread*> s_instance;

public:
    static bool start_thread();
    static void join_thread();
    static void process();
    static void process_wait_completion();
};

std::atomic<NetworkingThread*> NetworkingThread::s_instance{};

bool NetworkingThread::start_thread() {
    // This is called on the main thread.
    if (s_instance.load()) {
        return true;
    }

    auto* inst = new (std::nothrow) NetworkingThread;
    s_instance.store(inst, std::memory_order_release);

    inst->in_run.store(true);
    LightEvent_Init(&inst->in_wakeup, RESET_ONESHOT);
    LightEvent_Init(&inst->out_completed, RESET_STICKY);

    s32 priority = 0x30;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    // Make sure our networking thread has lower priority than the main thread
    // and audio thread.
    priority += 2;
    priority = std::min(std::max(priority, (s32)0x18), (s32)0x3f);

    inst->m_thread =
        threadCreate(net_thread_start, inst, 64 * 1024, priority, -1, false);
    if (!inst->m_thread) {
        fprintf(stderr, "Failed to start networking thread\n");
        return false;
    }

    return true;
}

void NetworkingThread::join_thread() {
    // This is called on the main thread.
    auto* inst = s_instance.exchange(nullptr);
    if (!inst) {
        return;
    }

    inst->in_run.store(false, std::memory_order_release);
    LightEvent_Signal(&inst->in_wakeup);
    threadJoin(inst->m_thread, U64_MAX);
    threadFree(inst->m_thread);
    delete inst;
}

void NetworkingThread::process() {
    // This is called on the main thread.
    auto* inst = s_instance.load(std::memory_order_relaxed);
    assert(inst);
    LightEvent_Signal(&inst->in_wakeup);
}

void NetworkingThread::process_wait_completion() {
    // This is called on the main thread.
    auto* inst = s_instance.load(std::memory_order_relaxed);
    assert(inst);

#ifdef TRACY_ENABLE
    ZoneScoped;
#endif
    inst->in_waiting_for_completion.store(true);
    LightEvent_Signal(&inst->in_wakeup);
    LightEvent_Wait(&inst->out_completed);
    inst->in_waiting_for_completion.store(false);
    LightEvent_Clear(&inst->out_completed);
}

void NetworkingThread::net_thread() {
#ifdef TRACY_ENABLE
    tracy::SetThreadName("Net thread");
#endif

    while (LightEvent_Wait(&in_wakeup),
           in_run.load(std::memory_order_acquire)) {
        bool waiting_for_completion = in_waiting_for_completion.load();
#ifdef TRACY_ENABLE
        ZoneScopedN("NetworkingThread::iter");
        if (waiting_for_completion) {
            ZoneText("Waiting for completion",
                     sizeof("Waiting for completion"));
        }
#endif

        linkUniversal->sendDeferredMessages();

        if (waiting_for_completion) {
            LightEvent_Clear(&in_wakeup);
            LightEvent_Signal(&out_completed);
        } else if (in_waiting_for_completion.load()) {
            // Do an extra iteration
            LightEvent_Signal(&in_wakeup);
        }
    }
#ifdef TRACY_ENABLE
    TracyMessageL("Net thread exited");
#endif
}

} // namespace

void start() { NetworkingThread::start_thread(); }

void stop() { NetworkingThread::join_thread(); }

void process() { NetworkingThread::process(); }

void process_wait_completion() { NetworkingThread::process_wait_completion(); }

} // namespace n3ds::networking
