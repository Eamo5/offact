#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <ps5/kernel.h>

#include "app_installer.h"
#include "notification.h"
#include "offactd.h"

#define INCASSET(name, file)                                                   \
    __asm__(".section .rodata\n"                                               \
            ".global " #name "\n"                                             \
            ".global " #name "_end\n"                                         \
            ".global " #name "_size\n"                                        \
            ".align 16\n" #name ":\n"                                         \
            ".incbin \"" file "\"\n" #name "_end:\n" #name "_size:\n"       \
            ".quad " #name "_end - " #name "\n"                               \
            ".previous\n");                                                    \
    extern const uint8_t name[];                                                \
    extern const size_t name##_size;

INCASSET(icon0_png, "sce_sys/icon0.png");

int sceAppInstUtilInitialize(void);
int sceAppInstUtilAppInstallAll(void *);

static int write_file(const char *path, const void *data, size_t size)
{
    FILE *f = fopen(path, "w");

    if (!f)
        return -1;
    if (fwrite(data, size, 1, f) != 1) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

static int needs_update(const char *path, const void *data, size_t size)
{
    struct stat st;
    FILE *f;
    uint8_t *buf;
    int mismatch;

    if (stat(path, &st) || st.st_size != (off_t)size)
        return 1;
    f = fopen(path, "r");
    if (!f)
        return 1;
    buf = malloc(size);
    if (!buf) {
        fclose(f);
        return 1;
    }
    mismatch = fread(buf, 1, size, f) != size || memcmp(buf, data, size) != 0;
    free(buf);
    fclose(f);
    return mismatch;
}

static int install_title_dir(const char *title_id, const char *dir)
{
    int (*sceAppInstUtilAppInstallTitleDir)(const char *, const char *, void *) = 0;
    uint32_t handle;

    if (!kernel_dynlib_handle(-1, "libSceAppInstUtil.sprx", &handle))
        sceAppInstUtilAppInstallTitleDir =
            (void *)kernel_dynlib_resolve(-1, handle, "Wudg3Xe3heE");
    if (sceAppInstUtilAppInstallTitleDir)
        return sceAppInstUtilAppInstallTitleDir(title_id, dir, 0);
    return sceAppInstUtilAppInstallAll(0);
}

int app_installer_install_if_needed(void)
{
    char base_dir[256];
    char sce_sys_dir[256];
    char param_path[256];
    char icon_path[256];
    char param_json[512];
    struct stat st;
    int err;
    size_t param_len;

    snprintf(param_json, sizeof(param_json),
             "{\n"
             "  \"titleId\": \"%s\",\n"
             "  \"deeplinkUri\": \"%s\",\n"
             "  \"localizedParameters\": {\n"
             "    \"defaultLanguage\": \"en-US\",\n"
             "    \"en-US\": { \"titleName\": \"%s\" }\n"
             "  }\n"
             "}\n",
             OFFACT_TITLE_ID, OFFACT_DASHBOARD_URL, OFFACT_APP_NAME);
    param_len = strlen(param_json);

    snprintf(base_dir, sizeof(base_dir), "/user/app/%s", OFFACT_TITLE_ID);
    snprintf(sce_sys_dir, sizeof(sce_sys_dir), "%s/sce_sys", base_dir);
    snprintf(param_path, sizeof(param_path), "%s/param.json", sce_sys_dir);
    snprintf(icon_path, sizeof(icon_path), "%s/icon0.png", sce_sys_dir);

    if (!stat(base_dir, &st) && !needs_update(param_path, param_json, param_len) &&
        !needs_update(icon_path, icon0_png, icon0_png_size))
        return 0;

    offact_notify("OffAct: installing launcher");
    if ((err = sceAppInstUtilInitialize()))
        return err;
    if (mkdir(base_dir, 0755) && errno != EEXIST)
        return -1;
    if (mkdir(sce_sys_dir, 0755) && errno != EEXIST)
        return -1;
    if (write_file(param_path, param_json, param_len))
        return -1;
    if (write_file(icon_path, icon0_png, icon0_png_size))
        return -1;
    err = install_title_dir(OFFACT_TITLE_ID, "/user/app/");
    if (!err)
        offact_notify("OffAct launcher ready");
    return err;
}
