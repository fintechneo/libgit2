const lg = require('./libgit2.js');
lg.onRuntimeInitialized = () => {
    const FS = lg.FS;
    const MEMFS = FS.filesystems.MEMFS;
    
    // Create bare repo
    FS.mkdir('/working');
    FS.mount(MEMFS, { root: '.' }, '/working');
    FS.chdir('/working');
    jsgitinit();
    jsgitinitrepo(1);
    console.log(FS.readdir('.'));
    
    console.log('Created bare repository');    
    jsgitshutdown();
    
    // Clone and create first commit
    jsgitinit();
    FS.mkdir('/working2');
    FS.mount(MEMFS, { root: '.' }, '/working2');
    FS.chdir('/working2');
    jsgitclone('/working', '.');
    FS.writeFile('test.txt', 'initial');
    jsgitadd('test.txt');
    jsgitcommit(
        'Revision 1',
        'emscripten', 'emscripten',
        new Date().getTime() / 1000,
        new Date().getTimezoneOffset()
    );
    jsgitpush();
    jsgitprintlatestcommit();
    console.log('First revision pushed');    
    
    // Create second commit
    FS.writeFile('test.txt', 'modified');
    jsgitadd('test.txt');
    jsgitcommit(
        'Revision 2',
        'emscripten', 'emscripten',
        new Date().getTime() / 1000,
        new Date().getTimezoneOffset()
    );
    
    jsgitshutdown();

    // Create conflict

    jsgitinit();
    FS.mkdir('/working3');
    FS.mount(MEMFS, { root: '.' }, '/working3');
    FS.chdir('/working3');
    jsgitclone('/working', '.');

    FS.writeFile('test.txt', 'conflict');
    jsgitadd('test.txt');
    jsgitcommit(
        'This will be a conflict',
        'emscripten', 'emscripten',
        new Date().getTime() / 1000,
        new Date().getTimezoneOffset()
    );
    jsgitpush();
    jsgitprintlatestcommit();
    jsgitshutdown();

    // Pull conflict into working2
    FS.chdir('/working2');
    jsgitinit();
    jsgitopenrepo('.');
    jsgitpull();

    console.log(FS.readFile('test.txt', {encoding: 'utf8'}));
};

