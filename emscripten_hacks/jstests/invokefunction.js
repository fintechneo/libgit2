


var func = function() { 
   console.log('I am javascript called from C world!'); 
};


const lg = require('../libgit2.js');
lg.onRuntimeInitialized = () => {

   var pointer=jsregisterfunction(func,'');

   jsinvokefunction(pointer);

}

