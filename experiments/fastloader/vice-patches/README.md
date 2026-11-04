# Isolated VICE integration patch

`espsette-fastloader.patch` targets VICE revision
`eaf9b61f5560f921ebaa25349b09e797f351eb1e`, following the separate-patch layout
used in Parobek. It registers the test tape-port device and fixes the released
SENSE input on C16 without a user port. The sender and adapter remain separate
in `sender.c`, `sender.h` and `vice-device.c`; the helper copies them into the
disposable emulator source after applying the integration patch.

```sh
python3 experiments/fastloader/patch-vice.py /path/to/disposable/vice-source
```

Patch paths have `a/vice/` and `b/vice/` prefixes. The helper uses `patch -p2`,
checks the complete patch before mutation and recognizes an already-applied
patch. Source and binaries stay in ignored `tools/fastloader-vice/`; the user's
Parobek tree is never changed. No emulator sources or binaries are distributed
with the firmware. The patch remains separate for final review after development.
