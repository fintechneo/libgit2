source ./emsdk/emsdk_env.sh
cd /libgit2/emscripten_hacks
rm libgit2.*
sh build.sh
node nodetest.js
node nodefstest.js