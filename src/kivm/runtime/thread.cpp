//
// Created by kiva on 2018/3/25.
//

#include <kivm/oop/method.h>
#include <kivm/runtime/thread.h>
#include <kivm/runtime/runtimeConfig.h>
#include <kivm/bytecode/interpreter.h>
#include <kivm/oop/primitiveOop.h>
#include <algorithm>

namespace kivm {
    Thread::Thread(Method *method, const std::list<oop> &args)
        : _frames(RuntimeConfig::get().threadMaxStackFrames),
          _method(method), _args(args),
          _javaThreadObject(nullptr), _nativeThread(nullptr),
          _pc(0), _state(ThreadState::RUNNING) {
    }

    void Thread::create(instanceOop javaThread) {
        this->_javaThreadObject = javaThread;
        if (this->shouldRecordInThreadTable()) {
            D("shouldRecordInThreadTable == true, recording.");
            Threads::add(this);
        }

        this->_nativeThread = new std::thread([this] {
            this->start();
        });
        this->onThreadLaunched();
    }

    void Thread::onThreadLaunched() {
        // Do nothing.
    }

    bool Thread::shouldRecordInThreadTable() {
        return true;
    }

    long Thread::getEetop() const {
        return (long) this->_nativeThread->native_handle();
    }

    int Thread::tryHandleException(instanceOop exceptionOop) {
        auto currentMethod = _frames.getCurrentFrame()->getMethod();
        int handler = currentMethod->findExceptionHandler(_pc,
            exceptionOop->getInstanceClass());

        if (handler > 0) {
            return handler;
        }

        this->_exceptionOop = exceptionOop;
        return -1;
    }

    Thread::~Thread() = default;

    JavaThread::JavaThread(Method *method, const std::list<oop> &args)
        : Thread(method, args) {
    }

    void JavaThread::start() {
        // No other threads will join this thread.
        // So it is OK to detach()
        this->_nativeThread->detach();

        // A thread must start with an empty frame
        assert(_frames.getSize() == 0);

        // Only one argument(this) in java.lang.Thread#run()
        assert(_args.size() == 1);

        runMethod(_method, _args);

        Threads::threadStateChangeLock().lock();
        this->setThreadState(ThreadState::DIED);
        Threads::threadStateChangeLock().unlock();

        if (this->shouldRecordInThreadTable()) {
            Threads::decAppThreadCountLocked();
        }
    }

    oop JavaThread::runMethod(Method *method, const std::list<oop> &args) {
        D("### JavaThread::runMethod(), maxLocals: %d, maxStack: %d",
            method->getMaxLocals(), method->getMaxStack());

        Frame frame(method->getMaxLocals(), method->getMaxStack());
        Locals &locals = frame.getLocals();

        // copy args to local variable table
        int localVariableIndex = 0;
        bool isStatic = method->isStatic();
        const std::vector<ValueType> descriptorMap = method->getArgumentValueTypes();

        std::for_each(args.begin(), args.end(), [&](oop arg) {
            if (arg == nullptr) {
                D("Copying reference: #%d - null", localVariableIndex);
                locals.setReference(localVariableIndex++, nullptr);
                return;
            }

            switch (arg->getMarkOop()->getOopType()) {
                case oopType::INSTANCE_OOP:
                case oopType::OBJECT_ARRAY_OOP:
                case oopType::TYPE_ARRAY_OOP: {
                    D("Copying reference: #%d - %p", localVariableIndex, arg);
                    locals.setReference(localVariableIndex++, arg);
                    break;
                }

                case oopType::PRIMITIVE_OOP: {
                    ValueType valueType = descriptorMap[isStatic ? localVariableIndex : localVariableIndex - 1];
                    switch (valueType) {
                        case ValueType::INT: {
                            int value = ((intOop) arg)->getValue();
                            D("Copying int: #%d - %d", localVariableIndex, value);
                            locals.setInt(localVariableIndex++, value);
                            break;
                        }
                        case ValueType::FLOAT: {
                            float value = ((floatOop) arg)->getValue();
                            D("Copying float: #%d - %f", localVariableIndex, value);
                            locals.setFloat(localVariableIndex++, value);
                            break;
                        }
                        case ValueType::DOUBLE: {
                            double value = ((doubleOop) arg)->getValue();
                            D("Copying double: #%d - %lf", localVariableIndex, value);
                            locals.setDouble(localVariableIndex++, value);
                            break;
                        }
                        case ValueType::LONG: {
                            long value = ((longOop) arg)->getValue();
                            D("Copying long: #%d - %ld", localVariableIndex, value);
                            locals.setLong(localVariableIndex++, value);
                            break;
                        }
                        default:
                            PANIC("Unknown value type: %d", valueType);
                            break;
                    }
                    break;
                }

                default:
                    PANIC("Unknown oop type");
            }
        });

        // give them to interpreter
        frame.setMethod(method);
        frame.setReturnPc(this->_pc);
        frame.setNativeFrame(method->isNative());

        this->_frames.push(&frame);
        this->_pc = 0;
        oop result = ByteCodeInterpreter::interp(this);
        this->_frames.pop();
        this->_pc = frame.getReturnPc();

        if (this->isExceptionOccurred()) {
            PANIC("Throw exception");
        }

        if (_frames.getSize() > 0) {
            auto returnTo = this->_frames.getCurrentFrame()->getMethod();
            D("returned from %s.%s:%s to %s.%s:%s",
                strings::toStdString(method->getClass()->getName()).c_str(),
                strings::toStdString(method->getName()).c_str(),
                strings::toStdString(method->getDescriptor()).c_str(),
                strings::toStdString(returnTo->getClass()->getName()).c_str(),
                strings::toStdString(returnTo->getName()).c_str(),
                strings::toStdString(returnTo->getDescriptor()).c_str());

        } else {
            D("returned from %s.%s:%s to the top method",
                strings::toStdString(method->getClass()->getName()).c_str(),
                strings::toStdString(method->getName()).c_str(),
                strings::toStdString(method->getDescriptor()).c_str());
        }
        return result;
    }

    Thread *Threads::currentThread() {
        Thread *found = nullptr;
        auto currentThreadID = std::this_thread::get_id();

        Threads::forEachAppThread([&](Thread *thread) {
            if (thread->getThreadState() != ThreadState::DIED) {
                auto checkThreadID = thread->_nativeThread->get_id();
                if (checkThreadID == currentThreadID) {
                    found = thread;
                    return true;
                }
            }
            return false;
        });
        return found;
    }
}
