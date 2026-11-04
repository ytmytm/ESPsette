# Post script: the platform upload command has already been selected here.
Import('env')
import json
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(env['PROJECT_DIR']) / 'scripts'))
from ota_image import ota_image, write_if_changed

# The core forces both floating-point stdio engines into every image. This
# firmware uses integer/string stdio; JSON has its own numeric serializer.
# Leave ordinary linker dependency resolution to retain anything really used.
flags = list(env['LINKFLAGS'])
filtered = []
index = 0
while index < len(flags):
    if flags[index] == '-u' and index + 1 < len(flags) and flags[index + 1] in ('_printf_float', '_scanf_float'):
        index += 2
    else:
        filtered.append(flags[index])
        index += 1
env.Replace(LINKFLAGS=filtered)


def prepare_ota(source, target, env):
    image = Path(env.subst('$BUILD_DIR/${PROGNAME}.bin'))
    objdump = env.subst('$OBJCOPY').replace('objcopy', 'objdump')
    symbols = subprocess.check_output([objdump, '-t', str(image.with_suffix('.elf'))],
                                      env=env['ENV'], text=True)
    fs_start = next(int(line.split()[0], 16) - 0x40200000 for line in symbols.splitlines()
                    if line.split() and line.split()[-1] == '_FS_start')
    compressed, budget = ota_image(image.read_bytes(), fs_start)
    write_if_changed(str(image) + '.gz', compressed)
    write_if_changed(image.with_suffix('.ota.json'), (json.dumps(budget, indent=2) + '\n').encode())
    print(f'OTA gzip {len(compressed)} B; sketch {budget["sketch_bytes"]} B; '
          f'sector-aligned staging margin {budget["margin_bytes"]} B')


env.AddPostAction('$BUILD_DIR/${PROGNAME}.bin', prepare_ota)
if env.subst('$UPLOAD_PROTOCOL') == 'espota':
    env.Replace(UPLOADCMD='"$PYTHONEXE" "$UPLOADER" $UPLOADERFLAGS -f "$BUILD_DIR/${PROGNAME}.bin.gz"')
    env.AddPreAction('upload', prepare_ota)
