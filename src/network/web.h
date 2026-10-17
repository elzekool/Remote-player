#pragma once

// Web pages (status, settings, file manager, usage), OTA updates and, in access
// point mode, the captive portal DNS.

// Starts OTA, mDNS and the web server. In access point mode it also starts the
// captive portal, which redirects every request to the settings page.
void setupWeb(bool accessPointMode);
// Services OTA, HTTP and DNS requests and performs a pending restart; call
// continuously.
void handleWeb();
