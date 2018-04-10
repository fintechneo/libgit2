emcmake cmake -DCMAKE_C_FLAGS="-O3" -DAPPLY_EMSCRIPTEN_HACKS=ON -DSONAME=OFF -DUSE_HTTPS=OFF -DBUILD_SHARED_LIBS=OFF -DTHREADSAFE=OFF -DBUILD_CLAR=OFF -DUSE_SSH=OFF ..
emcmake cmake -build .
emmake make
echo "building libgit2.js"
emcc -s WASM=1 -s ALLOW_MEMORY_GROWTH=1 --post-js jsinit.js -s EXPORTED_FUNCTIONS=@jslibexportedfunctions.json -O3 jslib.c libgit2.a -Isrc -I../src -I../include -o libgit2.js
