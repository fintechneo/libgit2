// "Stupid" web worker that accepts scripts
const startTime = new Date().getTime();
importScripts("libgit2.js");
Module['onRuntimeInitialized'] = () => {

    const dir="workdir";
    FS.mkdir(dir,"0777");
    FS.mount(IDBFS, {}, '/'+dir);
    FS.chdir("/"+dir);     
        
    console.log('libgit2 ready in webworker.',(new Date().getTime() - startTime),'ms startup time');
};