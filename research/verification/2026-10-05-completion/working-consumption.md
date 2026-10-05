# Native working-state consumption verification

Scope: bounded integration evidence from current working-source bytes. This is neither a holdout evaluation nor a quality or context-utility claim. Request, LLM proposal and activity remain input only. All eight targets are verified SOURCE bytes or EOS.

Protocol: four complete regular files, at most 131072 bytes each; answer length at least seven; eight distinct offsets floor(length*i/7), including EOS; all 2 current owners eligible (mask=3); 1024 physical B3/S23 Life generations; exactly eight domain updates; no pending tasks. No script runtime, hosted provider or tool execution.

State path: runs/local.clife

REQUEST: id=148 version=1 kind=0 split=TRAIN current=1 bytes=34 sha256=5ab0ce22957faccab88b071c5e213af3d174b3fc9dbe21ee45be610c8b3d7e40
path=data/train/completion-request.txt
attribution=completion verification/v1: visible user request; input only

LLM_PROPOSAL: id=149 version=1 kind=1 split=TRAIN current=1 bytes=1412 sha256=7fabe4fc0f9f555c2e32d3389bd836437bfb209504ca2529346ff7719f7a16f4
path=data/train/completion-llm-context.txt
attribution=completion verification/v1: attributed LLM proposal; input only

ACTIVITY: id=150 version=1 kind=2 split=TRAIN current=1 bytes=1271 sha256=8d4322e5ea4d98c017df7a2e2799316d97422d732a5d32b8bc58210eda21db00
path=data/train/completion-activity.txt
attribution=completion verification/v1: visible native work; input only

ANSWER_SOURCE: id=151 version=1 kind=0 split=TRAIN current=1 bytes=1031 sha256=e5a3b7ef0f9c72712485791e518d26d397b6b3c2823b1f07b3637d8902f6f66f
path=src/domain/algorithms.c
attribution=completion verification/v1: working source; source-byte teacher

Teacher 0: source-id=151 offset=0 target=35
Teacher 1: source-id=151 offset=147 target=32
Teacher 2: source-id=151 offset=294 target=112
Teacher 3: source-id=151 offset=441 target=32
Teacher 4: source-id=151 offset=589 target=112
Teacher 5: source-id=151 offset=736 target=117
Teacher 6: source-id=151 offset=883 target=32
Teacher 7: source-id=151 offset=1031 target=256 (EOS)

Generation: 11264 -> 12288
Updates: 480 -> 488 (delta=8)
Contacts: 11264 -> 12288 (delta=1024)
Pending: 0 -> 0
Completed: 480 -> 488
Owner 0 uid=1: clock=480 -> 488
Owner 1 uid=2: clock=480 -> 488
Shared clock=0 -> 0
World policy clock=0 -> 0

CODE readout values and both optimizer moments: bitwise unchanged.
Canonical CODE ownership/readout/moments SHA256 before=6a46e5f63e960b58761afd1199a47af9614984392fa3ec6266a2a605941c1238
after=6a46e5f63e960b58761afd1199a47af9614984392fa3ec6266a2a605941c1238
Routing can change when TEXT training changes centroids.

Publication order: this native report is written atomically and admitted as ACTIVITY input; the complete checkpoint is then saved atomically to the state path. Any failure before that final save preserves the incumbent disk checkpoint. The successful admission ID and report SHA256 are emitted after checkpoint publication.
