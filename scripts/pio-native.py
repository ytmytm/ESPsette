# PlatformIO pre-build hook: the native code must match the firmware source.
Import('env')
import subprocess
import sys
from pathlib import Path
subprocess.run([sys.executable, str(Path(env['PROJECT_DIR']) / 'scripts/build-native.py')], check=True)


def check_fastloader_iram(source, target, env):
    objdump = env.subst('$OBJCOPY').replace('objcopy', 'objdump')
    symbols = subprocess.check_output([objdump, '-t', '-C', str(target[0])],
                                      env=env['ENV'], text=True)
    for name in ('sendFastByte(', 'FastGpioOutput::wait('):
        matches = [line for line in symbols.splitlines() if name in line]
        if not matches or any(not line.split()[0].startswith('401') for line in matches):
            raise RuntimeError(f'Fastloader timing function must reside in IRAM: {name}')
    print('Fastloader GPIO transmitter and busy wait verified in IRAM')


env.AddPostAction('$BUILD_DIR/${PROGNAME}.elf', check_fastloader_iram)
