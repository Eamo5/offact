# OffAct

OffAct is now a self-contained PS5 ELF daemon. It starts a local WebUI at
`http://127.0.0.1:8085/`, installs/updates a home-screen browser launcher with
Title ID `OFFA00001`, and opens the dashboard in the PS5 browser when possible.

## Safety warning

Account activation changes registry values and may affect save-data ownership.
The dashboard never auto-selects an account and never bulk patches accounts.
Activation requires an explicit selected slot, a matching account-name
confirmation, and backend revalidation of the account snapshot immediately
before writing. The backend writes only the selected slot and verifies that no
other populated slot changed.

## Build

```sh
make
```

`PS5_PAYLOAD_SDK` must point at the PS5 payload SDK. The build embeds
`frontend/index.html` into `include/assets_index_html.h` and links the native
daemon as `OffAct.elf`.

## Dashboard endpoints

- `GET /` and `GET /index.html` serve the embedded WebUI.
- `GET /api/status` returns daemon metadata.
- `GET /api/accounts` lists populated account slots.
- `GET /api/diagnostics` returns read-only verbose account diagnostics that can
  be fetched from a remote PC for comparing a working activated account with a
  problematic account.
- `GET /api/deepdiff?from=1&to=4` performs a read-only diff of readable registry
  values between two account slots. String values are redacted to length/hash.
- `POST /api/debug/patch-int` is an advanced testing endpoint for patching one
  integer relative registry key on one explicitly selected test account slot.
- `POST /api/activate` activates one explicitly selected and revalidated slot.
- `POST /api/launcher/install` installs/updates the `OFFA00001` launcher.
- `POST /api/browser/open` opens the local dashboard.
- `POST /api/shutdown` stops the daemon.

No external network fetches, payload repositories, autoloading, or payload
upload features are included.
