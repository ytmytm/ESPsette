# RemoteDebug ships an old WebSockets copy that fails on ESP8266 Arduino 3.x
# BearSSL (no WiFiClientSecure::verify). Project uses Telnet debug only.
#
# Do not touch SRC_FILTER / lib builders here — that broke framework includes.
# Instead neutralize the removed BearSSL API in the vendored client source.
from pathlib import Path

Import("env")  # noqa: F821

old = "if(!_client.ssl->verify(_fingerprint.c_str(), _host.c_str()))"
new = "if(false /* ESP8266 BearSSL: verify() removed */)"

libdeps = Path(env["PROJECT_LIBDEPS_DIR"])  # noqa: F821
patched = 0
if libdeps.is_dir():
    for path in libdeps.rglob("WebSocketsClient.cpp"):
        text = path.read_text(encoding="utf-8", errors="replace")
        if old not in text:
            continue
        path.write_text(text.replace(old, new), encoding="utf-8")
        patched += 1
        print(f"filter_remotedebug: patched {path}")

if patched == 0:
    print("filter_remotedebug: no WebSocketsClient.cpp patch needed")
