const diff = require('./deep-diff.min.js');

function getJSONConflictVersion(text, pick) {
    const originalText = getConflictVersion(text, 1);
    if(pick===1) {        
        return originalText;
    }
    
    const original = JSON.parse(originalText);
    const mine = JSON.parse(getConflictVersion(text, 0));
    const yours = JSON.parse(getConflictVersion(text, 2));

    const minePatch = diff.diff(original, mine);
    const yourPatch = diff.diff(original, yours);

    
    if(pick===0) {
        yourPatch.forEach(change => diff.applyChange(original,null, change));
        minePatch.forEach(change => diff.applyChange(original,null, change));
    } else if(pick === 2) {
        minePatch.forEach(change => diff.applyChange(original,null, change));
        yourPatch.forEach(change => diff.applyChange(original,null, change));
    }
    return JSON.stringify(original, null, 1);
}

function getConflictVersion(text, pick) {
    while (hasConflicts(text)) {
        text = resolveNextConflict(text, pick);
    }
    return text;
}

function hasConflicts(text) {
    const lines = text.split('\n');
    return lines.findIndex((l) => l.startsWith('<<<<<<<')) > -1;
}

function resolveNextConflict(text, pick) {
    const lines = text.split('\n');
    const conflictMineIndex = lines.findIndex((l) => l.startsWith('<<<<<<<'));
    const conflictOldIndex = lines.findIndex((l) => l.startsWith('|||||||'));
    const conflictYoursIndex = lines.findIndex((l) => l.startsWith('======='));
    const conflictEndIndex = lines.findIndex((l) => l.startsWith('>>>>>>>'));

    let chosen;

    switch (pick) {
        case 0: // Mine
            chosen = lines.slice(conflictMineIndex + 1, conflictOldIndex);
            break;
        case 1: // Original
            chosen = lines.slice(conflictOldIndex + 1, conflictYoursIndex);
            break;
        case 2: // Yours (Incoming)
            chosen = lines.slice(conflictYoursIndex + 1, conflictEndIndex);
            break;
    }

    return lines.slice(0, conflictMineIndex)
        .concat(chosen)
        .concat(
            lines.slice(conflictEndIndex + 1, lines.length)
        ).join('\n');
}

module.exports = {
    getJSONConflictVersion: getJSONConflictVersion,
    getConflictVersion: getConflictVersion,
    hasConflicts: hasConflicts,
    resolveNextConflict: resolveNextConflict
}