#include "rb_json.h"

#include <math.h>

#include "rb_json_writer.h"

static void header(rb_json_writer_t *w, const rb_json_header_t *h, const char *type)
{
    rb_json_obj_begin(w);
    rb_json_key(w, "schema_version");
    rb_json_int(w, RB_JSON_SCHEMA_VERSION);
    rb_json_key(w, "type");
    rb_json_str(w, type);
    rb_json_key(w, "controller_id");
    rb_json_str(w, h->controller_id);
    rb_json_key(w, "timestamp");
    rb_json_str(w, h->timestamp);
    rb_json_key(w, "uptime_ms");
    rb_json_int(w, h->uptime_ms);
    rb_json_key(w, "sequence");
    rb_json_int(w, h->sequence);
}

static void presence_obj(rb_json_writer_t *w, const rb_telemetry_t *t)
{
    rb_json_key(w, "presence");
    rb_json_obj_begin(w);
    rb_json_key(w, "valid");
    rb_json_bool(w, t->presence.valid);
    /* Unknown presence is null, never "false" (missing data is not safe). */
    rb_json_key(w, "detected");
    t->presence.valid ? rb_json_bool(w, t->presence.detected) : rb_json_null(w);
    rb_json_key(w, "moving");
    t->presence.valid ? rb_json_bool(w, t->presence.moving) : rb_json_null(w);
    rb_json_key(w, "stationary");
    t->presence.valid ? rb_json_bool(w, t->presence.stationary) : rb_json_null(w);
    rb_json_key(w, "distance_m");
    rb_json_num(w, t->presence.valid ? t->presence.distance_m : NAN, 2);
    rb_json_obj_end(w);
}

static void thermal_obj(rb_json_writer_t *w, const rb_telemetry_t *t)
{
    const bool v = t->thermal.valid;
    rb_json_key(w, "thermal");
    rb_json_obj_begin(w);
    rb_json_key(w, "valid");
    rb_json_bool(w, v);
    rb_json_key(w, "max_c");
    rb_json_num(w, v ? t->thermal.max_c : NAN, 2);
    rb_json_key(w, "min_c");
    rb_json_num(w, v ? t->thermal.min_c : NAN, 2);
    rb_json_key(w, "mean_c");
    rb_json_num(w, v ? t->thermal.mean_c : NAN, 2);
    rb_json_key(w, "hot_region_c");
    rb_json_num(w, v ? t->thermal.hot_region_c : NAN, 2);
    rb_json_key(w, "rate_c_per_min");
    rb_json_num(w, v ? t->thermal.rate_c_per_min : NAN, 2);
    rb_json_key(w, "pixels_above_threshold");
    v ? rb_json_int(w, t->thermal.pixels_above_threshold) : rb_json_null(w);
    rb_json_obj_end(w);
}

static void safety_obj(rb_json_writer_t *w, const rb_telemetry_t *t)
{
    rb_json_key(w, "safety");
    rb_json_obj_begin(w);
    rb_json_key(w, "state");
    rb_json_str(w, t->safety.state);
    rb_json_key(w, "state_ms");
    rb_json_int(w, t->safety.state_ms);
    rb_json_key(w, "unattended_ms");
    rb_json_int(w, t->safety.unattended_ms);
    rb_json_key(w, "buzzer");
    rb_json_str(w, t->safety.buzzer);
    rb_json_key(w, "shutdown");
    rb_json_bool(w, t->safety.shutdown);
    rb_json_key(w, "test_timers");
    rb_json_bool(w, t->safety.test_timers);
    rb_json_obj_end(w);
}

static void faults_arr(rb_json_writer_t *w, const rb_telemetry_t *t, bool detailed)
{
    rb_json_key(w, "faults");
    rb_json_arr_begin(w);
    for (size_t i = 0; i < t->fault_count; i++) {
        if (detailed) {
            rb_json_obj_begin(w);
            rb_json_key(w, "name");
            rb_json_str(w, t->faults[i].name);
            rb_json_key(w, "class");
            rb_json_str(w, t->faults[i].fault_class);
            rb_json_obj_end(w);
        } else {
            rb_json_str(w, t->faults[i].name);
        }
    }
    rb_json_arr_end(w);
}

static void node_fields(rb_json_writer_t *w, const rb_telemetry_t *t)
{
    rb_json_key(w, "sensor_node");
    rb_json_str(w, t->sensor_node);
}

size_t rb_json_telemetry(const rb_telemetry_t *t, char *buf, size_t len)
{
    rb_json_writer_t w;
    rb_json_init(&w, buf, len);
    header(&w, &t->hdr, "telemetry");
    rb_json_key(&w, "protocol_version");
    rb_json_int(&w, t->protocol_version);
    node_fields(&w, t);
    rb_json_key(&w, "node_link");
    rb_json_str(&w, t->node.link);
    presence_obj(&w, t);
    thermal_obj(&w, t);
    safety_obj(&w, t);
    faults_arr(&w, t, false);
    rb_json_obj_end(&w);
    return rb_json_finish(&w);
}

size_t rb_json_heartbeat(const rb_telemetry_t *t, char *buf, size_t len)
{
    rb_json_writer_t w;
    rb_json_init(&w, buf, len);
    header(&w, &t->hdr, "heartbeat");
    rb_json_key(&w, "safety_state");
    rb_json_str(&w, t->safety.state);
    rb_json_key(&w, "safety_loop_count");
    rb_json_int(&w, t->safety.loop_count);
    rb_json_obj_end(&w);
    return rb_json_finish(&w);
}

size_t rb_json_faults(const rb_telemetry_t *t, char *buf, size_t len)
{
    rb_json_writer_t w;
    rb_json_init(&w, buf, len);
    header(&w, &t->hdr, "faults");
    faults_arr(&w, t, true);
    rb_json_obj_end(&w);
    return rb_json_finish(&w);
}

size_t rb_json_presence(const rb_telemetry_t *t, char *buf, size_t len)
{
    rb_json_writer_t w;
    rb_json_init(&w, buf, len);
    header(&w, &t->hdr, "presence");
    node_fields(&w, t);
    presence_obj(&w, t);
    rb_json_obj_end(&w);
    return rb_json_finish(&w);
}

size_t rb_json_thermal(const rb_telemetry_t *t, char *buf, size_t len)
{
    rb_json_writer_t w;
    rb_json_init(&w, buf, len);
    header(&w, &t->hdr, "thermal");
    node_fields(&w, t);
    thermal_obj(&w, t);
    rb_json_obj_end(&w);
    return rb_json_finish(&w);
}

size_t rb_json_node_status(const rb_telemetry_t *t, char *buf, size_t len)
{
    rb_json_writer_t w;
    rb_json_init(&w, buf, len);
    header(&w, &t->hdr, "node_status");
    node_fields(&w, t);
    rb_json_key(&w, "link");
    rb_json_str(&w, t->node.link);
    rb_json_key(&w, "presence_valid");
    rb_json_bool(&w, t->presence.valid);
    rb_json_key(&w, "thermal_valid");
    rb_json_bool(&w, t->thermal.valid);
    rb_json_key(&w, "node_fault_flags");
    rb_json_int(&w, t->node.fault_flags);
    rb_json_key(&w, "missed_packets");
    rb_json_int(&w, t->node.missed);
    rb_json_key(&w, "restarts");
    rb_json_int(&w, t->node.restarts);
    rb_json_obj_end(&w);
    return rb_json_finish(&w);
}

size_t rb_json_event(const rb_event_msg_t *e, char *buf, size_t len)
{
    rb_json_writer_t w;
    rb_json_init(&w, buf, len);
    header(&w, &e->hdr, "event");
    rb_json_key(&w, "event");
    rb_json_str(&w, e->event);
    rb_json_key(&w, "safety");
    rb_json_obj_begin(&w);
    rb_json_key(&w, "state");
    rb_json_str(&w, e->state);
    rb_json_key(&w, "from_state");
    rb_json_str(&w, e->from_state);
    rb_json_key(&w, "reason");
    rb_json_str(&w, e->reason);
    rb_json_key(&w, "unattended_ms");
    rb_json_int(&w, e->unattended_ms);
    rb_json_obj_end(&w);
    rb_json_key(&w, "fault");
    if (e->fault != NULL) {
        rb_json_obj_begin(&w);
        rb_json_key(&w, "name");
        rb_json_str(&w, e->fault);
        rb_json_key(&w, "class");
        rb_json_str(&w, e->fault_class);
        rb_json_obj_end(&w);
    } else {
        rb_json_null(&w);
    }
    rb_json_obj_end(&w);
    return rb_json_finish(&w);
}

size_t rb_json_controller_status(const char *controller_id, bool online, char *buf, size_t len)
{
    rb_json_writer_t w;
    rb_json_init(&w, buf, len);
    rb_json_obj_begin(&w);
    rb_json_key(&w, "schema_version");
    rb_json_int(&w, RB_JSON_SCHEMA_VERSION);
    rb_json_key(&w, "type");
    rb_json_str(&w, "controller_status");
    rb_json_key(&w, "controller_id");
    rb_json_str(&w, controller_id);
    rb_json_key(&w, "online");
    rb_json_bool(&w, online);
    rb_json_obj_end(&w);
    return rb_json_finish(&w);
}
