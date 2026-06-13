#include "browser.h"
#include "offactd.h"
#include "notification.h"

extern int sceSystemServiceLaunchWebBrowser(const char *uri);

int browser_open_dashboard(void)
{
    int err = sceSystemServiceLaunchWebBrowser(OFFACT_DASHBOARD_URL);

    if (err)
        offact_notify("OffAct: failed to open browser (0x%08X)", err);
    return err ? -1 : 0;
}
