#include "sms/server/httpd.h"

#include <string.h>

#include <microhttpd.h>

#include <flinter/types/tree.h>
#include <flinter/encode.h>

#include "sms/server/configure.h"
#include "sms/server/handler.h"

class HTTPD::Request {
public:
    Request() : _abandoned(false) {}
    std::string _payload;
    bool _abandoned;

}; // class Request

HTTPD::HTTPD(Processor *processor)
        : _daemon(nullptr)
        , _processor(processor)
{
    // Intended left blank
}

HTTPD::~HTTPD()
{
    // Intended left blank
}

static int httpd_handler(
        void *cls,
        struct MHD_Connection *connection,
        const char *url,
        const char *method,
        const char *version,
        const char *upload_data,
        size_t *upload_data_size,
        void **con_cls)
{
    HTTPD *const httpd = reinterpret_cast<HTTPD *>(cls);
    return httpd->Handler(
            connection,
            url,
            method,
            version,
            upload_data,
            upload_data_size,
            con_cls);
}

static void httpd_completed(
        void *cls,
        struct MHD_Connection *connection,
        void **con_cls,
        enum MHD_RequestTerminationCode toe)
{
    HTTPD *const httpd = reinterpret_cast<HTTPD *>(cls);
    return httpd->Completed(connection, con_cls, toe);
}

static std::string httpd_html_escape(
        const char *s,
        bool escape_apos)
{
    std::string r;
    const char *p;
    size_t needed;
    char *o;

    for (needed = 0, p = s; *p; ++p) {
        switch (*p) {
        case '\'' : needed += escape_apos ? 5 : 1; break;
        case '"'  : needed += 6; break;
        case '&'  : needed += 5; break;
        case '<'  : needed += 4; break;
        case '>'  : needed += 4; break;
        default   : needed += 1; break;
        }
    }

    r.resize(needed);
    for (o = &r[0]; *s; ++s) {
        switch (*s) {
        case '"' : memcpy(o, "&quot;", 6); o += 6; break;
        case '&' : memcpy(o, "&amp;",  5); o += 5; break;
        case '<' : memcpy(o, "&lt;",   4); o += 4; break;
        case '>' : memcpy(o, "&gt;",   4); o += 4; break;
        case '\'':
            if (escape_apos) {
                memcpy(o, "&#39;", 5);
                o += 5;
                break;
            }
            [[fallthrough]]
        default  : *o++ = *s; break;
        };
    }

    return r;
}

static struct MHD_Response *httpd_create_standard_response(
        unsigned int status_code,
        const char *extra,
        bool close)
{
    struct MHD_Response *r;
    std::ostringstream s;
    const char *status;

    switch (status_code) {
    case MHD_HTTP_FOUND:
        status = "Found";
        break;
    case MHD_HTTP_BAD_REQUEST:
        status = "Bad Request";
        break;
    case MHD_HTTP_FORBIDDEN:
        status = "Forbidden";
        break;
    case MHD_HTTP_NOT_FOUND:
        status = "Not Found";
        break;
    case MHD_HTTP_METHOD_NOT_ALLOWED:
        status = "Method Not Allowed";
        break;
    case MHD_HTTP_INTERNAL_SERVER_ERROR:
        status = "Internal Server Error";
        break;
    default:
        abort();
    }

    s << "<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 2.0//EN\">\n"
         "<html><head>\n"
         "<title>" << status_code << " " << status << "</title>\n"
         "</head><body>\n"
         "<h1>" << status << "</h1>\n"
         "<p>";

    switch (status_code) {
    case MHD_HTTP_FOUND:
        s << "The document has moved <a href=\""
          << httpd_html_escape(extra, true)
          << "\">here</a>.";
        break;

    case MHD_HTTP_BAD_REQUEST:
        s << "Your browser sent a request that this server could not "
             "understand.<br />\n";
        break;

    case MHD_HTTP_FORBIDDEN:
        s << "You don't have permission to access this resource.";
        break;

    case MHD_HTTP_NOT_FOUND:
        s << "The requested URL was not found on this server.";
        break;

    case MHD_HTTP_METHOD_NOT_ALLOWED:
        s << "The requested method "
          << httpd_html_escape(extra, false)
          << " is not allowed for this URL.";
        break;

    case MHD_HTTP_INTERNAL_SERVER_ERROR:
        s << "The server encountered an internal error or\n"
             "misconfiguration and was unable to complete\n"
             "your request.</p>\n"
             "<p>Please contact the server administrator at \n"
             " "
          << httpd_html_escape(extra, false)
          << " to inform them of the time this error occurred,\n"
             " and the actions you performed just before this error.</p>\n"
             "<p>More information about this error may be available\n"
             "in the server error log.";
        break;

    default:
        abort();
    }

    s << "</p>\n"
         "</body></html>\n";

    std::string &&str = s.str();
    r = MHD_create_response_from_buffer(
            str.length(),
            const_cast<char *>(str.c_str()),
            MHD_RESPMEM_MUST_COPY);

    if (!r) {
        return NULL;
    }

    if (MHD_add_response_header(r, MHD_HTTP_HEADER_SERVER,
                "Apache") != MHD_YES ||
        MHD_add_response_header(r, MHD_HTTP_HEADER_CONTENT_TYPE,
                "text/html; charset=iso-8859-1") != MHD_YES) {

        MHD_destroy_response(r);
        return NULL;
    }

    if (close) {
        if (MHD_add_response_header(r, MHD_HTTP_HEADER_CONNECTION, "close") != MHD_YES) {
            MHD_destroy_response(r);
            return NULL;
        }
    }

    return r;
}

static int httpd_standard_response(
        struct MHD_Connection *connection,
        unsigned int status_code,
        const char *extra,
        bool close)
{
    struct MHD_Response *r;
    int ret;

    r = httpd_create_standard_response(status_code, extra, close);
    if (!r) {
        return MHD_NO;
    }

    ret = MHD_queue_response(connection, status_code, r);
    MHD_destroy_response(r);
    return ret;
}

static struct MHD_Response *httpd_create_redirect_response(const char *url)
{
    struct MHD_Response *r;

    r = httpd_create_standard_response(MHD_HTTP_FOUND, url, false);
    if (!r) {
        return NULL;
    }

    if (MHD_add_response_header(r, MHD_HTTP_HEADER_LOCATION, url) != MHD_YES) {
        MHD_destroy_response(r);
        return NULL;
    }

    return r;
}

static int httpd_redirect(
        struct MHD_Connection *connection,
        const char *url)
{
    struct MHD_Response *r;
    int ret;

    r = httpd_create_redirect_response(url);
    if (!r) {
        return MHD_NO;
    }

    ret = MHD_queue_response(connection, MHD_HTTP_FOUND, r);
    MHD_destroy_response(r);
    return ret;
}

static int httpd_error(struct MHD_Connection *connection, const char *admin)
{
    return httpd_standard_response(
            connection,
            MHD_HTTP_INTERNAL_SERVER_ERROR,
            admin,
            true);
}

bool HTTPD::Start()
{
    const flinter::Tree &c = (*g_configure)["httpd"];

    const unsigned int flags = MHD_USE_ERROR_LOG
                             | MHD_USE_ITC
                             | MHD_USE_DUAL_STACK
                             | MHD_USE_AUTO_INTERNAL_THREAD;

    const uint16_t port = c["port"].as<uint16_t>();

    _daemon = MHD_start_daemon(
            flags, port,
            nullptr, nullptr,
            &httpd_handler, this,
            MHD_OPTION_NOTIFY_COMPLETED,
            &httpd_completed, this,
            MHD_OPTION_END);

    if (!_daemon) {
        return false;
    }

    return true;
}

void HTTPD::Stop()
{
    MHD_socket listener = MHD_quiesce_daemon(_daemon);
    MHD_stop_daemon(_daemon);
    if (listener != MHD_INVALID_SOCKET) {
        close(listener);
    }
}

int HTTPD::Handler(
        struct MHD_Connection *connection,
        const char *url,
        const char *method,
        const char *version,
        const char *upload_data,
        size_t *upload_data_size,
        void **con_cls)
{
    static const size_t kMaximum = 8192;

    (void)connection;
    (void)url;
    (void)version;

    if (!*con_cls) {
        *con_cls = new Request;
        return MHD_YES;
    }

    Request *const request = reinterpret_cast<Request *>(*con_cls);

    if (*upload_data_size) {
        if (request->_payload.size() + *upload_data_size > kMaximum) {
            return MHD_NO;
        }

        request->_payload.append(upload_data, *upload_data_size);
        *upload_data_size = 0;
        return MHD_YES;
    }

    if (strcmp(method, MHD_HTTP_METHOD_POST)) {
        return httpd_standard_response(connection,
                MHD_HTTP_METHOD_NOT_ALLOWED,
                method, false);
    }

    const char *const ct = MHD_lookup_connection_value(
            connection,
            MHD_HEADER_KIND,
            MHD_HTTP_HEADER_CONTENT_TYPE);

    if (!ct || strcmp(ct, "application/json")) {
        return httpd_standard_response(connection,
                MHD_HTTP_BAD_REQUEST,
                nullptr, false);
    }

    std::string response;
    int status = handle(request->_payload, &response, _processor);
    switch (status) {
    case MHD_HTTP_FOUND:
    case MHD_HTTP_MOVED_PERMANENTLY:
        return httpd_redirect(connection, response.c_str());

    case MHD_HTTP_FORBIDDEN:
    case MHD_HTTP_NOT_FOUND:
    case MHD_HTTP_BAD_REQUEST:
        return httpd_standard_response(connection,
                static_cast<unsigned int>(status), nullptr, false);

    case MHD_HTTP_ACCEPTED:
        break;

    default:
        return Error(connection);
    }

    struct MHD_Response *const r = MHD_create_response_from_buffer(
            response.length(),
            const_cast<char *>(response.c_str()),
            MHD_RESPMEM_MUST_COPY);

    if (!r) {
        return Error(connection);
    }

    if (MHD_add_response_header(r, MHD_HTTP_HEADER_SERVER,
                "Apache") != MHD_YES ||
        MHD_add_response_header(r, MHD_HTTP_HEADER_CACHE_CONTROL,
                "private, no-cache, no-store, must-revalidate, max-age=0") != MHD_YES ||
        MHD_add_response_header(r, MHD_HTTP_HEADER_PRAGMA,
                "no-cache") != MHD_YES ||
        MHD_add_response_header(r, MHD_HTTP_HEADER_CONTENT_TYPE,
                "application/json") != MHD_YES ){

        MHD_destroy_response(r);
        return Error(connection);
    }

    int ret = MHD_queue_response(connection, MHD_HTTP_ACCEPTED, r);
    MHD_destroy_response(r);
    return ret;
}

int HTTPD::Error(struct MHD_Connection *connection) const
{
    const flinter::Tree &c = (*g_configure)["httpd"];
    return httpd_error(connection, c["admin"].c_str());
}

void HTTPD::Completed(
        struct MHD_Connection *connection,
        void **con_cls,
        int toe)
{
    (void)connection;
    (void)toe;

    delete reinterpret_cast<Request *>(*con_cls);
}
