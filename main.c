#include <stdio.h>
#include <unistd.h>

#include <microhttpd.h>

#include "app_installer.h"
#include "browser.h"
#include "http_server.h"
#include "notification.h"
#include "offactd.h"

int sceNetCtlInit(void);
int sceUserServiceInitialize(void *);

int main(int argc, char **argv)
{
    struct MHD_Daemon *daemon;
    int err;

    (void)argc;
    (void)argv;

    sceUserServiceInitialize(NULL);
    sceNetCtlInit();

    daemon = MHD_start_daemon(MHD_USE_SELECT_INTERNALLY, OFFACT_PORT, NULL, NULL,
                              &http_on_request, NULL, MHD_OPTION_END);
    if (!daemon) {
        offact_notify("OffAct: failed to start web server");
        return 1;
    }

    offact_notify("OffAct running at %s", OFFACT_DASHBOARD_URL);

    err = app_installer_install_if_needed();
    if (err)
        offact_notify("OffAct: launcher install failed (0x%08X)", err);

    browser_open_dashboard();

    while (http_keep_running)
        sleep(1);

    MHD_stop_daemon(daemon);
    return 0;
}
