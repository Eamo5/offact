# OffAct - Native PS5 ELF daemon

PS5_HOST ?= ps5
PYTHON ?= python3

ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
    $(error PS5_PAYLOAD_SDK is undefined)
endif

VERSION_TAG := $(shell git describe --abbrev=10 --dirty --always --tags 2>/dev/null || echo dev)

ELF := OffAct.elf
ASSET_HEADER := include/assets_index_html.h
FRONTEND_INDEX := frontend/index.html

SRCS := main.c offact.c \
        src/http_server.c src/account_service.c src/activation_service.c \
        src/app_installer.c src/browser.c src/notification.c

CFLAGS := -O1 -g -Wall -Wno-format-truncation -DVERSION_TAG=\"$(VERSION_TAG)\" -Iinclude -I.
LDADD := -lmicrohttpd -lpthread -lSceRegMgr -lSceNetCtl -lSceUserService \
         -lSceSystemService -lSceAppInstUtil -lSceNet

all: $(ELF)

$(ASSET_HEADER): $(FRONTEND_INDEX) tools/gen_assets.py
	$(PYTHON) tools/gen_assets.py $(FRONTEND_INDEX) $(ASSET_HEADER) index_html

$(ELF): $(ASSET_HEADER) $(SRCS) sce_sys/icon0.png
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDADD)

clean:
	rm -f $(ELF) $(ASSET_HEADER) OffAct.zip

upload: $(ELF)
	curl -T $^ ftp://$(PS5_HOST):2121/data/homebrew/OffAct/$^

install: $(ELF) sce_sys/icon0.png
	install -Dm 644 sce_sys/icon0.png -t "${DESTDIR}/${PREFIX}/OffAct/sce_sys"
	install -Dm 755 $(ELF) -t "${DESTDIR}/${PREFIX}/OffAct"

dist: $(ELF) sce_sys/icon0.png
	zip -r OffAct.zip $^

.PHONY: all clean upload install dist
