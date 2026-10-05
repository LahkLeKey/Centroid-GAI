# Three-domain independently verified native C suite

Status: ok. Attempted 12/12; verified TRAIN candidates 12/12. Compiler: `clang`. Elapsed before publication: 4172 ms.

Project parent: `a25f20aead557c992d24feadcaeade99975c26bfd97e084686b0fe01fc30c0dc`. Evaluator: `e2bee2d67d8075cf87b00566baeb44adef9315c45590c6c5087a664eb9870a5b`. The SOURCE role is the exact current declared project file, never an earlier experiment winner.

TRAIN targets are selected from aggregate independent TRAIN outcomes only; each of two family receipts records that family's support for the selected action. DEV and AUDIT were opened only after all six contact updates and cannot fit the model.

| Catalog | Action | TRAIN failures/8 | TRAIN comparisons | DEV failures/4 | AUDIT failures/8 |
| --- | --- | --- | --- | --- | --- |
| affine | 0 | 4 | 0 | 2 | 3 |
| affine | 1 | 1 | 0 | 1 | 2 |
| affine | 2 | 3 | 0 | 1 | 1 |
| affine | 3 | 0 | 0 | 0 | 0 |
| lower-bound | 0 | 0 | 49 | 0 | 0 |
| lower-bound | 1 | 0 | 27 | 0 | 0 |
| lower-bound | 2 | 5 | 28 | 2 | 5 |
| lower-bound | 3 | 2 | 22 | 0 | 1 |
| saturation | 0 | 4 | 0 | 1 | 4 |
| saturation | 1 | 4 | 0 | 3 | 6 |
| saturation | 2 | 0 | 0 | 0 | 0 |
| saturation | 3 | 4 | 0 | 3 | 6 |

affine selected action=3; TRAIN winner probability 0.192218917931 -> 0.579091096721, cross entropy 1.64912035893 -> 0.546295479206. Frozen independent-use input choice 3 -> 2; winner probability 0.279485116936 -> 0.160138318782; AUDIT failures of model choice 0 -> 1. All fresh-input distributions were frozen before AUDIT consumption.

lower-bound selected action=1; TRAIN winner probability 0.215482300596 -> 0.416958950317, cross entropy 1.53487650462 -> 0.874767502517. Frozen independent-use input choice 3 -> 3; winner probability 0.247092031149 -> 0.146459988375; AUDIT failures of model choice 1 -> 1. All fresh-input distributions were frozen before AUDIT consumption.

saturation selected action=2; TRAIN winner probability 0.144345918897 -> 0.233816176105, cross entropy 1.93554264555 -> 1.45322004455. Frozen independent-use input choice 3 -> 3; winner probability 0.199754806465 -> 0.113451261265; AUDIT failures of model choice 6 -> 6. All fresh-input distributions were frozen before AUDIT consumption.

Life generations 10752 -> 11264; authentic contacts=512; owned updates=6; completed=6; pending=0. Publication gate: accepted. This demonstrates three authored finite C contracts and exact comparison-cost improvement, not general code generation or a wall-clock speedup. Fresh AUDIT families were not used in any prior range experiment. The independent-use model input is a new request over the same catalog, not a new unseen action language. Inspect every candidate, compile/train/dev/audit log, receipts, input.bin and generalization-input.bin. Applying accepted winner.c is a separate explicit action.
