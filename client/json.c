#include "json.h"

#include <stdio.h>

#include "parson.h"

static const char *g_token;

void json_set_token(const char *token)
{
    g_token = token;
}

int json_decode(const char *result, int *ret)
{
    JSON_Value *root_value;
    JSON_Object *root;

    root_value = json_parse_string(result);
    if (!root_value) {
        return -1;
    }

    if (json_value_get_type(root_value) != JSONObject) {
        return -1;
    }

    root = json_value_get_object(root_value);
    if (!json_object_has_value_of_type(root, "ret", JSONNumber)) {
        json_value_free(root_value);
        return -1;
    }

    *ret = (int)json_object_get_number(root, "ret");
    json_value_free(root_value);
    return 0;
}

int json_encode(
        const struct json_sms *sms,
        size_t sms_count,
        const struct json_call *calls,
        size_t call_count,
        void *buffer,
        size_t length)
{
    JSON_Value *root_value;
    JSON_Value *call_value;
    JSON_Value *pdu_value;
    JSON_Object *root;
    JSON_Array *call;
    JSON_Array *pdu;

    size_t i;

    root_value = json_value_init_object();
    root = json_value_get_object(root_value);

    pdu = NULL;
    pdu_value = NULL;
    for (i = 0; i < sms_count; ++i) {
        const struct json_sms *m;
        char timestamp[32];
        JSON_Value *value;
        JSON_Object *obj;

        value = json_value_init_object();
        obj = json_value_get_object(value);

        m = sms + i;
        sprintf(timestamp, "%ld%09ld", m->when.tv_sec, m->when.tv_nsec);

        json_object_set_string(obj, "timestamp", timestamp);
        json_object_set_string(obj, "type", m->type);
        json_object_set_string(obj, "pdu", m->pdu);

        if (!pdu) {
            pdu_value = json_value_init_array();
            pdu = json_value_get_array(pdu_value);
        }

        json_array_append_value(pdu, value);
    }

    call = NULL;
    call_value = NULL;
    for (i = 0; i < call_count; ++i) {
        const struct json_call *c;
        char timestamp[32];
        char duration[32];
        JSON_Value *value;
        JSON_Object *obj;
        long d;

        value = json_value_init_object();
        obj = json_value_get_object(value);

        c = calls + i;
        d = (long)(c->call_end.tv_sec - c->call_start.tv_sec) * 1000000000
          + (long)(c->call_end.tv_nsec - c->call_start.tv_nsec);

        sprintf(timestamp, "%ld%09ld", c->ring_start.tv_sec, c->ring_start.tv_nsec);
        sprintf(duration, "%ld", d);

        json_object_set_string(obj, "timestamp", timestamp);
        json_object_set_string(obj, "duration", duration);
        json_object_set_string(obj, "peer", c->peer);
        json_object_set_string(obj, "type", c->type);
        json_object_set_string(obj, "raw", c->raw);

        if (!call) {
            call_value = json_value_init_array();
            call = json_value_get_array(call_value);
        }

        json_array_append_value(call, value);
    }

    json_object_set_string(root, "token", g_token);

    if (pdu_value) {
        json_object_set_value(root, "pdu", pdu_value);
    }

    if (call_value) {
        json_object_set_value(root, "call", call_value);
    }

    if (json_serialize_to_buffer(root_value, buffer, length)) {
        json_value_free(root_value);
        return -1;
    }

    json_value_free(root_value);
    return 0;
}
