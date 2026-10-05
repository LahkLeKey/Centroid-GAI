# Native working-state consumption verification

Scope: bounded integration evidence from current working-source bytes. This is neither a holdout evaluation nor a quality or context-utility claim. Request, LLM proposal and activity remain input only. All eight targets are verified SOURCE bytes or EOS.

Protocol: four complete regular files, at most 131072 bytes each; answer length at least seven; eight distinct offsets floor(length*i/7), including EOS; all 2 current owners eligible (mask=3); 1024 physical B3/S23 Life generations; exactly eight domain updates; no pending tasks. No script runtime, hosted provider or tool execution.

State path: runs/local.clife

REQUEST: id=175 version=1 kind=0 split=TRAIN current=1 bytes=200 sha256=a996652018df0dd63069883886a105cb5d6141c16e0c62d2153be09acaa79f0a
path=data/train/blueprint-closure-request.txt
attribution=completion verification/v1: visible user request; input only

LLM_PROPOSAL: id=176 version=1 kind=1 split=TRAIN current=1 bytes=973 sha256=a88bf582e016a74f079feaa88887e3c2a49474831e78e47e4db8686ba9207f5e
path=data/train/blueprint-closure-llm.txt
attribution=completion verification/v1: attributed LLM proposal; input only

ACTIVITY: id=177 version=1 kind=2 split=TRAIN current=1 bytes=856 sha256=2394db99bb50d23d132bd0518692358a0f2df00348c811f12eb55632cc185723
path=data/train/blueprint-closure-activity.txt
attribution=completion verification/v1: visible native work; input only

ANSWER_SOURCE: id=178 version=1 kind=0 split=TRAIN current=1 bytes=3084 sha256=9ded230e7dce68a25bd461c8ded310d35c502cfc97d23ade03dc3600bc472d05
path=src/domain/source.c
attribution=completion verification/v1: working source; source-byte teacher

Teacher 0: source-id=178 offset=0 target=35
Teacher 1: source-id=178 offset=440 target=32
Teacher 2: source-id=178 offset=881 target=32
Teacher 3: source-id=178 offset=1321 target=77
Teacher 4: source-id=178 offset=1762 target=99
Teacher 5: source-id=178 offset=2202 target=101
Teacher 6: source-id=178 offset=2643 target=110
Teacher 7: source-id=178 offset=3084 target=256 (EOS)

Generation: 14336 -> 15360
Updates: 616 -> 624 (delta=8)
Contacts: 14336 -> 15360 (delta=1024)
Pending: 0 -> 0
Completed: 616 -> 624
Owner 0 uid=1: clock=616 -> 624
Owner 1 uid=2: clock=616 -> 624
Shared clock=0 -> 0
World policy clock=0 -> 0

CODE readout values and both optimizer moments: bitwise unchanged.
Canonical CODE ownership/readout/moments SHA256 before=6a46e5f63e960b58761afd1199a47af9614984392fa3ec6266a2a605941c1238
after=6a46e5f63e960b58761afd1199a47af9614984392fa3ec6266a2a605941c1238
Routing can change when TEXT training changes centroids.

Publication order: this native report is written atomically and admitted as ACTIVITY input; the complete checkpoint is then saved atomically to the state path. Any failure before that final save preserves the incumbent disk checkpoint. The successful admission ID and report SHA256 are emitted after checkpoint publication.
