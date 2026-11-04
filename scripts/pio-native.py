# PlatformIO pre-build hook: the native code must match the firmware source.
Import('env')
import subprocess
import sys
import re
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
    decoder_symbols = [line for line in symbols.splitlines() if 'WriteDecoder::' in line]
    if not decoder_symbols or any(not line.split()[0].startswith('401') for line in decoder_symbols):
        raise RuntimeError('WRITE decoder must reside entirely in IRAM')
    disassembly = subprocess.check_output([objdump, '-d', '-C', str(target[0])],
                                         env=env['ENV'], text=True)
    in_write = False
    for line in disassembly.splitlines():
        label = re.match(r'^[0-9a-f]+ <(.+)>:', line)
        if label:
            in_write = 'WriteDecoder::' in label[1] or label[1].startswith((
                'onWriteEdge(', 'onMotorChange(', 'onMotorFalling(',
                'writeCommandsAllowed(', 'setWriteInterruptMode(', 'updateWriteMotor('))
        if in_write and re.search(r'\bcall0\s+402[0-9a-f]+', line):
            raise RuntimeError('WRITE ISR calls flash-resident code: ' + line.strip())
    print('WRITE decoder and direct ISR calls verified in IRAM/ROM')


env.AddPostAction('$BUILD_DIR/${PROGNAME}.elf', check_fastloader_iram)
