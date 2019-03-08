emcmake cmake -DCMAKE_C_FLAGS="-Oz" -DAPPLY_EMSCRIPTEN_HACKS=ON -DSONAME=OFF -DUSE_HTTPS=OFF -DBUILD_SHARED_LIBS=OFF -DTHREADSAFE=OFF -DBUILD_CLAR=OFF -DUSE_SSH=OFF ..
emcmake cmake -build .
emmake make
if [ $1 = "nodejs" ]
then
    echo "building libgit2.js (nodejs)"
    emcc -DEMSCRIPTEN_NODEJS=1 -s ALLOW_MEMORY_GROWTH=1 --post-js jsinit.js -s "EXTRA_EXPORTED_RUNTIME_METHODS=['FS']" -Oz jslib.c libgit2.a -Isrc -I../src -I../include -o libgit2.js
else
    echo "building libgit2.js (browser)"
    emcc -s ALLOW_MEMORY_GROWTH=1 --post-js jsinit.js -s "EXTRA_EXPORTED_RUNTIME_METHODS=['FS']" -Oz jslib.c libgit2.a -Isrc -I../src -I../include -o libgit2.js
fi