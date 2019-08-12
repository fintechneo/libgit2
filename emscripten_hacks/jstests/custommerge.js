const lg = require('../libgit2.js');



var func = function(ancestor,ours,thiers) { 
    console.log('I am going to be the call back!'); 
};


lg.onRuntimeInitialized = () => {
    const FS = lg.FS;
    const MEMFS = FS.filesystems.MEMFS;
    
    var pointer=jsregisterfunction(func,'sss');

    // Create bare repo
    FS.mkdir('/working');
    FS.mount(MEMFS, { root: '.' }, '/working');
    FS.chdir('/working');
    jsgitinit();
    jsgitinitrepo(1);
    jsgitsetuser('Test user','test@example.com');
  //   console.log(FS.readdir('.'));
    
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
    jsonobj.commonobject.rhsproperty = 'rhs modified';
    
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

//  register custom driver
    const line="*.json merge=custom\n"
    jsregisterdriver(pointer);
    FS.writeFile(".gitattributes", line);
    const jsonM = jsgitattrget('test.json','merge');
    console.log(`test.json merge=${jsonM}`);
    const textM = jsgitattrget('test.text','merge');
    console.log(`test.text merge=${textM}`);
//
    
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

    
}