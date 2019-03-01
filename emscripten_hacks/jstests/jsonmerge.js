const diff3 = require('../jslibs/diff3.js');

const lg = require('../libgit2.js');
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
    
    let jsonobj = {
        'lhs': 'lhs original',
        'rhs': 'rhs original',
        'common': 'common original',
        'commonarray': ['original'],
        'commonobject': {
            'lhsproperty': 'lhs original property',
            'rhsproperty': 'rhs original property',
            'commonproperty': 'common original property',
        }
    };
    // Clone and create first commit
    jsgitinit();

    FS.mkdir('/working2');
    FS.mount(MEMFS, { root: '.' }, '/working2');
    FS.chdir('/working2');
    jsgitclone('/working', '.');
    jsgitsetuser('Test user','test@example.com');
    FS.writeFile('test.json', JSON.stringify(jsonobj, null, 1));
    jsgitadd('test.json');

    jsgitcommit(
        'Original revision'
    );
    jsgitpush();
    jsgitprintlatestcommit();
    console.log('First revision pushed');    
    
    jsonobj.lhs = 'lhs modified';
    jsonobj.common = 'lhs modified';
    jsonobj.commonarray.push('lhs added');
    jsonobj.commonobject.lhsproperty = 'lhs modified';
    jsonobj.commonobject.commonproperty = 'lhs modified';
    // Create second commit
    FS.writeFile('test.json', JSON.stringify(jsonobj, null, 1));
    jsgitadd('test.json');

    jsgitcommit(
        'lhs modified'
    );
    
    jsgitshutdown();

    // Create conflict

    jsgitinit();
    FS.mkdir('/working3');
    FS.mount(MEMFS, { root: '.' }, '/working3');
    FS.chdir('/working3');
    jsgitclone('/working', '.');
    jsgitsetuser('Test user','test@example.com');
    
    jsonobj = JSON.parse(FS.readFile('test.json', {encoding: 'utf8'}));
    jsonobj.common = 'rhs modified';
    jsonobj.rhs = 'rhs modified';
    jsonobj.commonobject.rhsproperty = 'rhs modified';
    jsonobj.commonobject.commonproperty = 'rhs modified';
    jsonobj.commonarray.push('rhs added');
    jsonobj.commonarray.push('only rhs added 2');

    FS.writeFile('test.json', JSON.stringify(jsonobj, null, 1));
    jsgitadd('test.json');

    jsgitcommit(
        'This will be a conflict'
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
        
    console.log('Should show a conflict here');
    jsgitstatus();
    console.log(jsgitstatusresult);

    if(jsgitstatusresult.length === 0) {
        throw('Should be a conflict');
    }
    const conflict = FS.readFile('test.json', {encoding: 'utf8'});
    console.log(conflict);

    // Resolve conflict

    const resolvedLHS = diff3.getJSONConflictVersion(conflict, 0);
    const resolvedLHSObj = JSON.parse(resolvedLHS);
    const resolvedRHS = diff3.getJSONConflictVersion(conflict, 2);
    const resolvedRHSObj = JSON.parse(resolvedRHS);

    console.log('Resolved version LHS');
    console.log(resolvedLHSObj);

    console.log('Resolved version RHS');
    console.log(resolvedRHSObj);

    const assertResolvedLHS =
        resolvedLHSObj.rhs === 'rhs modified' &&
        resolvedLHSObj.lhs === 'lhs modified' &&
        resolvedLHSObj.common === 'lhs modified';
    const assertResolvedRHS =
        resolvedRHSObj.rhs === 'rhs modified' &&
        resolvedRHSObj.lhs === 'lhs modified' &&
        resolvedRHSObj.common === 'rhs modified';

    if(assertResolvedLHS && assertResolvedRHS) {
        console.log('JSON merge test passed');
    } else {
        throw('JSON merge test failed');
    }
};

