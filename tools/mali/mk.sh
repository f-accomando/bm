# usage: mk.sh OUT.h  (builds ~/mkshader from tools/mali/mkshader.c with the Mesa tree of ~/mb and runs it)
set -e
W=$PWD; cd ~/mb
python3 - <<'PY'
import json, shlex
cc = json.load(open('compile_commands.json'))
e = [c for c in cc if c['file'].endswith('bifrost_compile.c')][0]
a = shlex.split(e['command'])
out = []
i = 1
while i < len(a):
    x = a[i]
    if x in ('-o', '-MF', '-MQ', '-MT'): i += 2; continue
    if x == '-c' or x.endswith('bifrost_compile.c') or x == '-MD': i += 1; continue
    out.append(x); i += 1
open('/tmp/mk_flags.txt', 'w').write(' '.join(shlex.quote(x) for x in out))
PY
L=src
LIBS="$L/panfrost/compiler/libpanfrost_compiler.a $L/compiler/nir/libnir.a $L/compiler/libcompiler.a $L/util/libmesa_util.a $L/panfrost/compiler/bifrost/libpanfrost_bifrost.a $L/panfrost/shared/libpanfrost_shared.a $L/util/blake3/libblake3.a $L/panfrost/model/libpanfrost_model_lib.a $L/panfrost/compiler/bifrost/libpanfrost_bifrost_disasm.a $L/panfrost/compiler/bifrost/valhall/libpanfrost_valhall_disasm.a $L/panfrost/compiler/midgard/libpanfrost_midgard.a $L/panfrost/compiler/midgard/libpanfrost_midgard_disasm.a"
SRC="$W/tools/mali/mkshader.c"
eval gcc -O1 -w $(cat /tmp/mk_flags.txt) -I$HOME/mesa-bifrost/src/compiler -I$HOME/mesa-bifrost/src/panfrost/compiler -o ~/mkshader "$SRC" -Wl,--start-group $LIBS -Wl,--end-group -lz -lexpat -lm -lpthread -lstdc++
~/mkshader "$W/$1"
