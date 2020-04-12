#ifndef SMS_SERVER_PROCESSOR_H
#define SMS_SERVER_PROCESSOR_H

#include <stdint.h>

#include <chrono>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "sms/server/db.h"

class Splitter;

class Processor {
public:
    Processor();
    ~Processor();

    bool Initialize();
    bool Shutdown();
    bool Cleanup();

    int Received(std::unique_ptr<db::Call> r);
    int Received(std::unique_ptr<db::PDU > r);
    int Received(std::unique_ptr<db::SMS > r);

protected:
    class Task {
    public:
        std::chrono::steady_clock::time_point _when;
        std::unique_ptr<db::Call> _call;
        std::unique_ptr<db::PDU>  _pdu;
        std::unique_ptr<db::SMS>  _sms;
    }; // class Task

    class Device {
    public:
        std::chrono::steady_clock::time_point _flush;
        std::list<db::Call> _call;
        std::list<db::SMS> _sms;
        std::string _receiver;
        std::string _to;
    }; // class Done

    static std::string FormatTime(int64_t t);
    static std::string FormatDate(int64_t t);
    static std::string FormatDateTime(int64_t t);
    static std::string FormatDuration(int64_t t);
    static std::string Format(const db::SMS &sms);
    static std::string Format(const db::Call &call);
    static std::string Format(
            const std::list<db::Call> &call,
            const std::list<db::SMS> &sms);

    static bool Send(
            const std::string &to,
            const std::string &receiver,
            const std::string &mail);

    void Split(const std::chrono::steady_clock::time_point &when);
    void InitializeDevices();
    void Flush(bool force);

    void Finish(const std::chrono::steady_clock::time_point &when,
                const db::SMS &sms);

    void Finish(const std::chrono::steady_clock::time_point &when,
                const db::Call &call);

private:
    // Only access from cleanup thread, no need to lock
    std::unordered_map<int, Device> _devices;
    Splitter *const _splitter;

    // Access from both server thread and cleanup thread
    std::list<Task> _tasks;
    std::mutex _mutex;

}; // class Processor

#endif // SMS_SERVER_PROCESSOR_H
