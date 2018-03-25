//
// Created by kiva on 2018/3/25.
//

#include <kivm/runtime/thread.h>
#include <kivm/runtime_config.h>

namespace kivm {
    Thread::Thread(Method *method, const std::list<oop> &args)
            : _frames(RuntimeConfig::get().threadInitialStackSize),
              _method(method), _args(args),
              _java_thread_object(nullptr), _native_thread(nullptr),
              _pc(nullptr), _state(ThreadState::RUNNING) {
    }

    void Thread::create(instanceOop java_thread) {
        this->_java_thread_object = java_thread;
        this->_native_thread = new std::thread([this] {
            this->start();
        });
        this->thread_lunched();
    }

    void Thread::thread_lunched() {
        // Do nothing.
    }

    Thread::~Thread() = default;

    JavaMainThread::JavaMainThread()
            : Thread(nullptr, {}) {
    }

    void JavaMainThread::start() {
        // TODO: call main(String[])
    }

    void JavaMainThread::thread_lunched() {
        // Start the first app thread to run main(String[])
        this->_native_thread->join();

        // Then, let's wait for all app threads to finish
        for (;;) {
            int threads = Threads::get_app_thread_count_locked();
            assert(threads >= 0);

            if (threads == 0) {
                break;
            }

            sched_yield();
        }
    }

    JavaThread::JavaThread(Method *method, const std::list<oop> &args)
            : Thread(method, args) {
    }

    void JavaThread::start() {
    }
}
