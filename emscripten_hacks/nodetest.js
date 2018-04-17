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
    jsgitsetuser('Test user','test@example.com');
    console.log(FS.readdir('.'));
    
    console.log('Created bare repository');    
    jsgitshutdown();
    
    // Clone and create first commit
    jsgitinit();
    FS.mkdir('/working2');
    FS.mount(MEMFS, { root: '.' }, '/working2');
    FS.chdir('/working2');
    jsgitclone('/working', '.');
    jsgitsetuser('Test user','test@example.com');
    FS.writeFile('test.txt', 'line1\nline2\n\line3');
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
    FS.writeFile('test.txt', 'line1\nline2 modify 1\n\line3');
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
    jsgitsetuser('Test user','test@example.com');
    FS.writeFile('test.txt', 'line1\nline2 modify 2 pick me\n\line3');
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

    jsgitprintlatestcommit();
        
    
    const conflict = FS.readFile('test.txt', {encoding: 'utf8'});
    console.log(conflict);

    // Resolve conflict

    const lines = conflict.split('\n');
    const ourStartIndex = lines.findIndex(line => line.indexOf('<<<<<<<')===0);
    const previousStartIndex = lines.findIndex(line => line.indexOf('|||||||')===0);
    const theirStartIndex = lines.findIndex(line => line.indexOf('=======')===0);
    const theirEndIndex = lines.findIndex(line => line.indexOf('>>>>>>>')===0);
    
    // Remove their version conflict marker
    lines.splice(theirEndIndex, 1);

    // Remove our version
    lines.splice(ourStartIndex, theirStartIndex - ourStartIndex + 1);
    
    lines.push('This is the resolved version');
    
    const resolved = lines.join('\n');
    console.log('Resolved version:');
    console.log(resolved);

    FS.writeFile('test.txt', resolved);
    
    jsgitadd('test.txt');
    
    jsgitresolvemergecommit();
    jsgitprintlatestcommit();

    jsgitpush();
    
    jsgitshutdown();
    
    // Pull back to workdir3
    FS.chdir('/working3');
    
    jsgitinit();
    jsgitopenrepo();
    
    jsgitpull();
    jsgitprintlatestcommit();
    
    const latest = FS.readFile('test.txt', {encoding: 'utf8'});
    console.log(latest);    

    FS.writeFile('test.txt', 'Total change');
    jsgitadd('test.txt');
    jsgitcommit(
        'Total change',
        'emscripten', 'emscripten',
        new Date().getTime() / 1000,
        new Date().getTimezoneOffset()
    );
    jsgitpush();
    jsgitshutdown();

    // Normal merge after conflicts
    FS.chdir('/working2');
    jsgitinit();
    jsgitopenrepo('.');
    
    FS.writeFile('test2.txt', 'New file');
    jsgitadd('test2.txt');
    jsgitcommit(
        'New file',
        'emscripten', 'emscripten',
        new Date().getTime() / 1000,
        new Date().getTimezoneOffset()
    );
    
    jsgitpull();

    jsgitprintlatestcommit();
        
    
    console.log(FS.readFile('test.txt', {encoding: 'utf8'}));

    jsgitshutdown();
};

