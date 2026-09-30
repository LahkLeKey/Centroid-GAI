import assert from 'node:assert/strict';
import test from 'node:test';
import { fileReferences, matchingTasks } from './repository-context.ts';

const tasks = [
    { id: 'snapshot-pass', title: 'Review snapshot retention', keywords: ['snapshot', 'retention'], priority: 1 },
    { id: 'model-corpus', title: 'Review neural training examples', keywords: ['neural', 'training'], priority: 2 },
    { id: 'worker-budget', title: 'Measure native worker memory', keywords: ['native', 'memory'], priority: 3 },
];

test('an exact task choice in one clause preserves a differently named alternative in another clause', () => {
    assert.deepEqual(matchingTasks(tasks, 'Should I work on snapshot-pass or neural training?').map(task => task.id),
        ['snapshot-pass', 'model-corpus']);
    assert.deepEqual(matchingTasks(tasks, 'Native memory versus model-corpus?').map(task => task.id),
        ['worker-budget', 'model-corpus']);
    assert.deepEqual(matchingTasks(tasks, 'Review snapshot retention and worker-budget').map(task => task.id),
        ['snapshot-pass', 'worker-budget']);
});

test('exact IDs still win within their own clause and repeated alternatives are deduplicated', () => {
    assert.deepEqual(matchingTasks(tasks, 'snapshot-pass with neural training details').map(task => task.id), ['snapshot-pass']);
    assert.deepEqual(matchingTasks(tasks, 'snapshot-pass with neural training details or native memory').map(task => task.id),
        ['snapshot-pass', 'worker-budget']);
    assert.deepEqual(matchingTasks(tasks, 'snapshot-pass or snapshot retention').map(task => task.id), ['snapshot-pass']);
    assert.deepEqual(matchingTasks(tasks, 'neural and training').map(task => task.id), ['model-corpus']);
});

test('dot-prefixed source directories keep their complete identity and unknown directories cannot borrow a basename', () => {
    const paths = ['.github/workflows/ci.yml', '.config/build.json', 'src/widget.ts', 'test/widget.ts', 'persistence/api/Dockerfile'];
    assert.deepEqual(fileReferences('Explain `.github/workflows/ci.yml` and .config/build.json.', paths),
        { found: ['.github/workflows/ci.yml', '.config/build.json'], missing: [] });
    assert.deepEqual(fileReferences('Use absent/widget.ts or .missing/build.json.', paths),
        { found: [], missing: ['absent/widget.ts', '.missing/build.json'] });
    assert.deepEqual(fileReferences('Choose widget.ts or persistence/api/Dockerfile.', paths),
        { found: ['src/widget.ts', 'test/widget.ts', 'persistence/api/Dockerfile'], missing: [] });
});
