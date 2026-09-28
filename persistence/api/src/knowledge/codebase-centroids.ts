/** Offline operator entry point for static knowledge models and centroid neighbors. */
import {parseArgs} from 'node:util';

import {
    buildCodebaseRelease,
    loadCodebaseSpatial,
    queryCodebaseRelease,
    verifyCodebaseRelease
} from './codebase-release.ts';

const {values} = parseArgs({
    options : {
        snapshot : {type : 'string'},
        output : {type : 'string'},
        encyclopedia : {type : 'string'},
        directory : {type : 'string'},
        verify : {type : 'boolean'},
        nearest : {type : 'string'},
        category : {type : 'string'},
        limit : {type : 'string'}
    }
});
if (values.snapshot && values.output && !values.directory && !values.verify && !values.nearest &&
    !values.category && !values.limit) {
    console.log(JSON.stringify(
        buildCodebaseRelease(values.snapshot, values.output, values.encyclopedia), null, 2));
} else if (values.directory && !values.snapshot && !values.output && !values.encyclopedia &&
           (!!values.verify !== !!values.nearest)) {
    if (values.nearest)
        console.log(JSON.stringify(
            queryCodebaseRelease(loadCodebaseSpatial(values.directory), values.nearest,
                                 values.limit === undefined ? 5 : Number(values.limit),
                                 values.category),
            null, 2));
    else {
        if (values.category || values.limit)
            throw new Error('Category/limit require --nearest');
        const release = verifyCodebaseRelease(values.directory);
        console.log(JSON.stringify({
            verified : true,
            sourceCommit : release.manifest.sourceCommit,
            ...release.manifest.statistics,
            centroids : release.manifest.centroids,
            spatial : release.audit,
            ...release.size
        },
                                   null, 2));
    }
} else
    throw new Error(
        'Usage: --snapshot <snapshot> --output <new-release> [--encyclopedia <release>] | --directory <release> --verify | --directory <release> --nearest <id> [--category <category>] [--limit 5]');
