A hack to build libgit2 with emscripten and run in the browser
==============================================================

The build-setup here is tested with Ubuntu Linux 16.04 and latest emscripten sdk (1.38.28)

First of all you need to source the emscripten sdk:

    source /home/ubuntu/emsdk_portable/emsdk_env.sh

Then go into the jsbuild folder and run the build shell script:

    cd emscripten_hacks
    sh build.sh

You should end up with files `libgit2.js` / `libgit2.wasm` and `libgit2_node.js` / `libgit2_node.wasm` in your jsbuild folder.

Because of CORS restrictions in the browser you cannot read from github directly from another domain. You need to add a proxy on your web server. You can run the githttpproxy.js script in this folder to 
get a local webserver with proxy to github.com:

    node githttpproxy.js

Navigate your browser to `http://localhost:5000`

When testing with the index.html file included here you should open the web console which will prompt "ready" when loaded. Remember to switch to the libgit2 webworker for typing commands in the console.

Type the commands:

    jsgitinit();
    jsgitclone("https://github.com/pathto/mygitrepo.git","mygitrepo");

You'll see the git clone process starts. This is not a small repository so it takes some time.

When the clone is done you can list the folder contents by typing:

    FS.readdir("mygitrepo")

A simple demonstration video can be seen here: https://youtu.be/rcBluzpUWE4

## https transport for nodejs

