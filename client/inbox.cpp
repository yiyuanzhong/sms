extern "C" {
#include "inbox.h"
} // extern "C"

#include <assert.h>
#include <string.h>

#include <list>
#include <string>

#include <flinter/thread/condition.h>
#include <flinter/thread/fixed_thread_pool.h>
#include <flinter/thread/mutex.h>
#include <flinter/thread/mutex_locker.h>
#include <flinter/runnable.h>
#include <flinter/utility.h>

extern "C" {
#include "http.h"
#include "json.h"
#include "logger.h"
#include "sms.h"
} // extern "C"

class Call {
public:
    explicit Call(const struct json_call *call)
            : _ring_start(call->ring_start)
            , _call_start(call->call_start)
            , _call_end(call->call_end)
            , _type(call->type)
            , _peer(call->peer)
            , _raw(call->raw)
    {
        // Intended left blank
    }

    const struct timespec _ring_start;
    const struct timespec _call_start;
    const struct timespec _call_end;
    const std::string _type;
    const std::string _peer;
    const std::string _raw;
}; // class Call

class Message {
public:
    Message(int index, const struct timespec *when)
            : _index(index), _when(*when)
    {
        // Intended left blank
    }

    const int _index;
    std::string _type;
    std::string _what;
    const struct timespec _when;

}; // class Message

class Inbox {
public:
    Inbox(struct sms *sms, struct http *h);

    int HealthCheck();
    int Commit(const char *what);
    int Commit(const struct json_call *call);
    int Prepare(int index, const struct timespec *when);

    bool Initialize();
    void Shutdown();

private:
    class Worker;
    bool Thread();
    bool Send(std::list<Message> *m,
              std::list<int> *done,
              std::list<Call> *c) const;

    Message *_incoming;
    struct http *const _h;
    struct sms *const _sms;

    std::list<int> _done;
    std::list<Call> _calls;
    std::list<Message> _messages;
    flinter::FixedThreadPool _pool;
    flinter::Condition _condition;
    flinter::Mutex _mutex;
    bool _quit;

}; // class Inbox

class Inbox::Worker : public flinter::Runnable {
public:
    virtual ~Worker() override {}
    explicit Worker(Inbox *inbox) : _inbox(inbox) {}
    virtual bool Run() override;

private:
    Inbox *const _inbox;

}; // class Inbox::Worker

Inbox::Inbox(struct sms *sms, struct http *h)
        : _incoming(nullptr), _h(h), _sms(sms), _quit(false)
{
    // Inteneded left blank
}

int Inbox::Prepare(int index, const struct timespec *when)
{
    assert(index > 0);
    assert(!_incoming);
    _incoming = new Message(index, when);
    return 0;
}

int Inbox::Commit(const char *what)
{
    assert(_incoming);
    Message &m = *_incoming;
    _incoming = nullptr;

    m._what = what;
    m._type = "Incoming"; // TODO(yiyuanzhong): emmm..

    LOGI("Inbox: time=%ld.%09ld index=%d type=[%s] what=[%s]",
            m._when.tv_sec, m._when.tv_nsec, m._index,
            m._type.c_str(), m._what.c_str());

    flinter::MutexLocker locker(&_mutex);
    _messages.push_back(m);
    _condition.WakeOne();
    return 0;
}

int Inbox::Commit(const struct json_call *call)
{
    Call c(call);

    flinter::MutexLocker locker(&_mutex);
    _calls.push_back(c);
    _condition.WakeOne();
    return 0;
}

int Inbox::HealthCheck()
{
    std::list<int> done;

    flinter::MutexLocker locker(&_mutex);
    if (_done.empty()) {
        return 0;
    }

    done.splice(done.end(), _done);
    locker.Unlock();

    for (auto i : done) {
        int ret = sms_delete_sms(_sms, i);
        if (ret) {
            return ret;
        }
    }

    return 0;
}

bool Inbox::Initialize()
{
    if (!_pool.Initialize(1)) {
        return false;
    }

    if (!_pool.AppendJob(new Worker(this), true)) {
        _pool.Shutdown();
        return false;
    }

    return true;
}

void Inbox::Shutdown()
{
    flinter::MutexLocker locker(&_mutex);
    _quit = true;
    _condition.WakeAll();
    locker.Unlock();

    _pool.Shutdown();
}

bool Inbox::Send(
        std::list<Message> *m,
        std::list<int> *done,
        std::list<Call> *c) const
{
    struct json_call jc[20];
    struct json_sms js[20];
    size_t i;
    size_t j;

    auto ps = m->begin();
    for (i = 0; i < sizeof(js) / sizeof(*js) && ps != m->end(); ++i, ++ps) {
        memset(js + i, 0, sizeof(*js));
        js[i].type = ps->_type.c_str();
        js[i].pdu = ps->_what.c_str();
        js[i].when = ps->_when;
    }

    auto pc = c->begin();
    for (j = 0; j < sizeof(jc) / sizeof(*jc) && pc != c->end(); ++j, ++pc) {
        memset(jc + j, 0, sizeof(*jc));
        jc[j].ring_start = pc->_ring_start;
        jc[j].call_start = pc->_call_start;
        jc[j].call_end = pc->_call_end;
        jc[j].type = pc->_type.c_str();
        jc[j].peer = pc->_peer.c_str();
        jc[j].raw = pc->_raw.c_str();
    }

    LOGI("Inbox: sending %lu messages and %lu calls", i, j);

    char request[16384];
    if (json_encode(js, i, jc, j, request, sizeof(request))) {
        return false;
    }

    int status;
    char response[64];
    if (http_perform(_h, request, &status, response, sizeof(response))) {
        return false;
    }

    if (status != 202) {
        return false;
    }

    int ret;
    if (json_decode(response, &ret)) {
        return false;
    }

    if (ret != 0) {
        return false;
    }

    for (auto q = m->begin(); q != ps; ++q) {
        done->push_back(q->_index);
    }

    m->erase(m->begin(), ps);
    c->erase(c->begin(), pc);
    LOGT("Inbox: sent %lu messages and %lu calls", i, j);
    return true;
}

bool Inbox::Thread()
{
    constexpr int64_t kRetry = 15000000000LL; // 15s

    std::list<Message> m;
    std::list<int> done;
    std::list<Call> c;

    size_t tries = 0;
    int64_t schedule = -1;
    flinter::MutexLocker locker(&_mutex);
    while (!_quit) {
        c.splice(c.end(), _calls);
        m.splice(m.end(), _messages);

        const int64_t now = get_monotonic_timestamp();
        if (schedule < 0) {
            if (m.empty() && c.empty()) {
                _condition.Wait(&_mutex);
                continue;
            }
        } else {
            const int64_t diff = schedule - now;
            if (diff > 0) {
                _condition.Wait(&_mutex, diff);
                continue;
            }
        }

        locker.Unlock();

        schedule = -1;
        if (Send(&m, &done, &c)) {
            tries = 0;

        } else {
            ++tries;
            LOGW("Inbox: sending failure, retry count: %lu", tries);
            schedule = get_monotonic_timestamp() + kRetry;
        }

        locker.Relock();
        _done.splice(_done.end(), done);
    }

    return true;
}

bool Inbox::Worker::Run()
{
    return _inbox->Thread();
}

struct inbox *inbox_initialize(struct sms *sms, struct http *h)
{
    Inbox *const inbox = new Inbox(sms, h);
    if (!inbox->Initialize()) {
        delete inbox;
        return nullptr;
    }

    return reinterpret_cast<struct inbox *>(inbox);
}

void inbox_shutdown(struct inbox *ptr)
{
    Inbox *const inbox = reinterpret_cast<Inbox *>(ptr);
    inbox->Shutdown();
    delete inbox;
}

int inbox_prepare(struct inbox *ptr, int index, const struct timespec *when)
{
    Inbox *const inbox = reinterpret_cast<Inbox *>(ptr);
    return inbox->Prepare(index, when);
}

int inbox_commit(struct inbox *ptr, const char *what)
{
    Inbox *const inbox = reinterpret_cast<Inbox *>(ptr);
    return inbox->Commit(what);
}

int inbox_call(struct inbox *ptr, const struct json_call *call)
{
    Inbox *const inbox = reinterpret_cast<Inbox *>(ptr);
    return inbox->Commit(call);
}

int inbox_health_check(struct inbox *ptr)
{
    Inbox *const inbox = reinterpret_cast<Inbox *>(ptr);
    return inbox->HealthCheck();
}
