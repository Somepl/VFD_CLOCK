#ifndef REMOTE_CLIENT_H
#define REMOTE_CLIENT_H

#include <Arduino.h>
#include "config.h"

enum RemoteState {
    REMOTE_DISABLED,
    REMOTE_CONNECTING,
    REMOTE_CONNECTED,
    REMOTE_DISCONNECTED
};

void remote_init();
void remote_update();

RemoteState remote_get_state();
String remote_get_state_str();
String remote_get_worker_url();
String remote_get_password();
bool remote_is_enabled();
void remote_set_config(const String& url, const String& password);
void remote_disable();

#endif
