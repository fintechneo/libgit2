emcmake cmake ..
emcmake cmake -build .
emmake make
echo "building libgit2.js"
emcc -s EMTERPRETIFY_ASYNC=1 -s EMTERPRETIFY=1 -s ALLOW_MEMORY_GROWTH=1 -s EXPORTED_FUNCTIONS="['_gitclonetest']" -O2 test.c libgit2.a deps/zlib/libzlib.a deps/http-parser/libhttp-parser.a -I../include -o libgit2.js
