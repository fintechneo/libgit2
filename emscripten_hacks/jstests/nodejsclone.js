const lg = require('../libgit2.js');
lg.onRuntimeInitialized = () => {
    const FS = lg.FS;
    const MEMFS = FS.filesystems.MEMFS;
    
    // Create bare repo
    FS.mkdir('/working');
    FS.mount(MEMFS, { root: '.' }, '/working');
    FS.chdir('/working');
    jsgitinit();
    jsgitclone('https://github.com/fintechneo/browsergittestdata.git', 'testdata');
    console.log(FS.readdir('testdata'));
}