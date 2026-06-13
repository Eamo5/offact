#pragma once

#ifndef VERSION_TAG
#define VERSION_TAG "dev"
#endif

#define OFFACT_APP_NAME "OffAct"
#define OFFACT_TITLE_ID "OFFA00001"
#define OFFACT_PORT 8085
#define OFFACT_VERSION VERSION_TAG

#define OFFACT_STRINGIFY2(x) #x
#define OFFACT_STRINGIFY(x) OFFACT_STRINGIFY2(x)

#define OFFACT_ROUTE_INDEX "/"
#define OFFACT_ROUTE_INDEX_HTML "/index.html"
#define OFFACT_ROUTE_ICON "/icon.png"
#define OFFACT_ROUTE_STATUS "/api/status"
#define OFFACT_ROUTE_ACCOUNTS "/api/accounts"
#define OFFACT_ROUTE_DIAGNOSTICS "/api/diagnostics"
#define OFFACT_ROUTE_DEEPDIFF "/api/deepdiff"
#define OFFACT_ROUTE_DEBUG_PATCH_INT "/api/debug/patch-int"
#define OFFACT_ROUTE_ACTIVATE "/api/activate"
#define OFFACT_ROUTE_LAUNCHER_INSTALL "/api/launcher/install"
#define OFFACT_ROUTE_BROWSER_OPEN "/api/browser/open"
#define OFFACT_ROUTE_SHUTDOWN "/api/shutdown"

#define OFFACT_DASHBOARD_URL "http://127.0.0.1:" OFFACT_STRINGIFY(OFFACT_PORT) "/"
