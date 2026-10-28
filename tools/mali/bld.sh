set -e
[ -d ~/mesa-bifrost ] || git clone -q --depth 1 https://github.com/lfdevs/mesa-for-android-container.git ~/mesa-bifrost
cd ~/mesa-bifrost
[ -f ~/mb/build.ninja ] || meson setup ~/mb -Dgallium-drivers=panfrost -Dvulkan-drivers= -Dllvm=enabled \
  -Dmesa-clc=enabled -Dinstall-mesa-clc=false -Dglx=disabled -Degl=disabled -Dgbm=disabled -Dplatforms= \
  -Dgles1=disabled -Dgles2=disabled -Dbuildtype=release -Dvalgrind=disabled -Dlibunwind=disabled \
  -Dlmsensors=disabled -Dzstd=disabled
ninja -C ~/mb src/panfrost/compiler/libpanfrost_compiler.a src/compiler/nir/libnir.a \
  src/compiler/libcompiler.a src/util/libmesa_util.a src/panfrost/compiler/bifrost/libpanfrost_bifrost.a \
  src/panfrost/shared/libpanfrost_shared.a src/util/blake3/libblake3.a src/panfrost/model/libpanfrost_model_lib.a \
  src/panfrost/compiler/bifrost/libpanfrost_bifrost_disasm.a \
  src/panfrost/compiler/bifrost/valhall/libpanfrost_valhall_disasm.a \
  src/panfrost/compiler/midgard/libpanfrost_midgard.a src/panfrost/compiler/midgard/libpanfrost_midgard_disasm.a
echo BUILD_DONE
