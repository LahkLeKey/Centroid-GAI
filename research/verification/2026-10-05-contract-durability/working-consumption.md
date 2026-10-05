# Native working-state consumption verification

Scope: bounded integration evidence from current working-source bytes. This is neither a holdout evaluation nor a quality or context-utility claim. Request, LLM proposal and activity remain input only. All eight targets are verified SOURCE bytes or EOS.

Protocol: four complete regular files, at most 131072 bytes each; answer length at least seven; eight distinct offsets floor(length*i/7), including EOS; all 2 current owners eligible (mask=3); 1024 physical B3/S23 Life generations; exactly eight domain updates; no pending tasks. No script runtime, hosted provider or tool execution.

State path: runs/local.clife

REQUEST: id=175 version=1 kind=0 split=TRAIN current=1 bytes=200 sha256=a996652018df0dd63069883886a105cb5d6141c16e0c62d2153be09acaa79f0a
path=data/train/blueprint-closure-request.txt
attribution=completion verification/v1: visible user request; input only

LLM_PROPOSAL: id=226 version=1 kind=1 split=TRAIN current=1 bytes=1001 sha256=24f441a7405f962d1ebe9914afb6a6ab1c1c8e9ed2aaf7b508368ee6c3501d73
path=data/train/contract-durability-llm.txt
attribution=completion verification/v1: attributed LLM proposal; input only

ACTIVITY: id=227 version=1 kind=2 split=TRAIN current=1 bytes=1062 sha256=aff2ace982658974cdf00a90406e69794e218efe082a33f8cf9c82c7bb3dc35f
path=data/train/contract-durability-activity.txt
attribution=completion verification/v1: visible native work; input only

ANSWER_SOURCE: id=228 version=1 kind=0 split=TRAIN current=1 bytes=4749 sha256=4b50f3fa248a323f38cba026ab23e442ba87327f2b72748fb0cea2fbf1e98dbe
path=src/experiment/contact.c
attribution=completion verification/v1: working source; source-byte teacher

Teacher 0: source-id=228 offset=0 target=35
Teacher 1: source-id=228 offset=678 target=60
Teacher 2: source-id=228 offset=1356 target=32
Teacher 3: source-id=228 offset=2035 target=41
Teacher 4: source-id=228 offset=2713 target=97
Teacher 5: source-id=228 offset=3392 target=95
Teacher 6: source-id=228 offset=4070 target=101
Teacher 7: source-id=228 offset=4749 target=256 (EOS)

Generation: 15616 -> 16640
Updates: 627 -> 635 (delta=8)
Contacts: 15616 -> 16640 (delta=1024)
Pending: 0 -> 0
Completed: 627 -> 635
Owner 0 uid=1: clock=627 -> 635
Owner 1 uid=2: clock=627 -> 635
Shared clock=0 -> 0
World policy clock=0 -> 0

CODE readout values and both optimizer moments: bitwise unchanged.
Canonical CODE ownership/readout/moments SHA256 before=0454e3479b7755210f90c737ef2dc9497f333829dcd9a2c5c585e20317eb0fe9
after=0454e3479b7755210f90c737ef2dc9497f333829dcd9a2c5c585e20317eb0fe9
Routing can change when TEXT training changes centroids.

Publication order: this native report is written atomically and admitted as ACTIVITY input; the complete checkpoint is then saved atomically to the state path. Any failure before that final save preserves the incumbent disk checkpoint. The successful admission ID and report SHA256 are emitted after checkpoint publication.
