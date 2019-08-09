// PJL  test the recontruction of files by using diff3 on the conflict file

const diff3 = require('../jslibs/diff3.js');
const lg = require('../libgit2.js');
var seedrandom = require('seedrandom');
var rng = seedrandom('hello.');
var fs = require('fs');


// divide x into about 'n' chunks and randomly reassemble
function shuffle(x,n) {  

    const nTot=x.length;
    const fract= 2 * nTot / n;

    let chunks=[];

    let i0=0;
    while ( i0 < nTot) {
        let n = (rng()*fract) | 0 ;
        if ( n === 0 ) {
            n=1;
        }
        let i1=i0+n;
        let chunk=x.slice(i0,i1);
        chunks.push(chunk);
        i0=i1;
    }

    let nChunk=chunks.length;
    let ret=[];

    while( nChunk > 0) {
        const iChunk = (nChunk*rng()) | 0;
        ret = ret.concat(chunks[iChunk]);
        chunks=chunks.slice(0,iChunk).concat(chunks.slice(iChunk+1));
        nChunk=chunks.length;
    }

    if (ret.length !== x.length) {
        throw new Error(" Check the shuffle routine");
    }
    return ret;
}


function addElements(x,n,start) {
    
    ret=x.slice();
    for(let i=0;i<n;i++){
        ret.push(start+i);
    }
	return ret;
}


let norig = 1000;
let nlhs  = 1000;
let nrhs  = 1000;
let nshuf =   20;

lg.onRuntimeInitialized = () => {
	const FS = lg.FS;
	const MEMFS = FS.filesystems.MEMFS;

	// Create bare repo
	FS.mkdir('/working');
	FS.mount(MEMFS, {
		root: '.'
	}, '/working');
	FS.chdir('/working');
	jsgitinit();
	jsgitinitrepo(1);
	jsgitsetuser('Test user', 'test@example.com');
	console.log(FS.readdir('.'));
	console.log('Created bare repository');
	jsgitshutdown();


	// Clone and create first commit
    const orig = addElements([],norig,0);

	jsgitinit();
	FS.mkdir('/working2');
	FS.mount(MEMFS, {
		root: '.'
	}, '/working2');
	FS.chdir('/working2');
	jsgitclone('/working', '.');
	jsgitsetuser('Test user', 'test@example.com');
	FS.writeFile('test.json', JSON.stringify(orig, null, 1));
	jsgitadd('test.json');

	jsgitcommit(
		'Original revision'
	);
	jsgitpush();
	jsgitprintlatestcommit();
	console.log('First revision pushed');

    let lhs = addElements(orig,nlhs,norig);
    lhs = shuffle(lhs,nshuf);

    const origLHS = JSON.stringify(lhs, null, 1);

	// Create second commit
	FS.writeFile('test.json',origLHS );
	jsgitadd('test.json');

	jsgitcommit(
		'lhs modified'
	);

	jsgitshutdown();

	// Create conflict

	jsgitinit();
	FS.mkdir('/working3');
	FS.mount(MEMFS, {
		root: '.'
	}, '/working3');
	FS.chdir('/working3');
	jsgitclone('/working', '.');
	jsgitsetuser('Test user', 'test@example.com');

    let rhs = addElements(orig,nrhs,norig+nlhs);
    rhs = shuffle(rhs,nshuf);

    const origRHS = JSON.stringify(rhs, null, 1);
	FS.writeFile('test.json', origRHS );
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

	if (jsgitstatusresult.length === 0) {
		throw ('Should be a conflict');
    }
    
	const conflict = FS.readFile('test.json', {
		encoding: 'utf8'
	});

	// Resolve conflict

	const resolvedLHS = diff3.getConflictVersion(conflict, 0);
    const resolvedRHS = diff3.getConflictVersion(conflict, 2);
    
    if ( resolvedLHS  !== origLHS) {
        console.error(" LHS was not resolved properly ");
        fs.writeFileSync('resolvedLHS',resolvedLHS);
        fs.writeFileSync('origLHS',origLHS);
        fs.writeFileSync('conflict',conflict);
    }
    
    if ( resolvedRHS  !== origRHS) {
        console.error(" RHS was not resolved properly ");
        fs.writeFileSync('resolvedRHS',resolvedRHS);
        fs.writeFileSync('origRHS',origRHS);
        fs.writeFileSync('conflict',conflict);
    }
    
};
