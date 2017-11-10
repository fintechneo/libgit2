emcmake cmake -DAPPLY_EMSCRIPTEN_HACKS=ON -DSONAME=OFF -DUSE_HTTPS=OFF -DBUILD_SHARED_LIBS=OFF -DTHREADSAFE=OFF -DBUILD_CLAR=OFF -DUSE_SSH=OFF ..
emcmake cmake -build .
emmake make
echo "building libgit2.js"
emcc -s EMTERPRETIFY_ASYNC=1 -s EMTERPRETIFY=1  -s ALLOW_MEMORY_GROWTH=1 -s EXPORTED_FUNCTIONS=@jslibexportedfunctions.json -O2 jslib.c libgit2.a -Isrc -I../src -I../include -o libgit2.js
