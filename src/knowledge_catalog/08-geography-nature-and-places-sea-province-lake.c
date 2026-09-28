/* Curated static knowledge baseline; maintain these C sources directly. */
/* Category: Geography, Nature, and Places: Sea, Province, Lake; centroids: 3. */
#include "../knowledge_catalog.h"

static const cgai_static_knowledge_centroid centroids[] = {
    {
        .id = "encyclopedia:cluster-005-0001:0011", .category = 8U, .observations = UINT64_C(5982),
        .description = "Geography, Nature, and Places; article-title cues include Sea, Province, Lake, Park, Mount, Storm",
        .cluster = 24U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0523749553F, 0.0720364898F, -0.0643432140F, 0.0685495958F,
            -0.0743316337F, 0.0710513517F, -0.0522271954F, 0.0731300488F,
            -0.0568864644F, -0.0600879379F, 0.0672589391F, -0.0502473265F,
            0.0441695191F, 0.0599205196F, 0.0359936692F, -0.0774937421F,
            0.0698890388F, 0.0473512970F, 0.0822515786F, 0.0462282673F,
            0.0710119754F, -0.0527788252F, 0.0634174198F, -0.0614670441F,
            0.0473413691F, -0.0545618348F, -0.0457850471F, 0.0380327180F,
            -0.0475187115F, 0.0689928681F, -0.0579601377F, -0.0529069006F
        }
    },
    {
        .id = "encyclopedia:cluster-005-0001:0013", .category = 8U, .observations = UINT64_C(6943),
        .description = "Geography, Nature, and Places; article-title cues include Sea, Province, Lake, Park, Mount, Storm; observed target words include City (58), Area (57), River (53), Region (52)",
        .cluster = 25U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0728783756F, 0.0994427353F, -0.0594093092F, 0.0973295718F,
            0.0439543799F, -0.0527978912F, -0.0596809573F, 0.0819509178F,
            -0.0785899460F, -0.0860501304F, 0.0891900510F, 0.0799905658F,
            0.0665894374F, -0.0479263589F, -0.0746690929F, -0.0875097737F,
            -0.0499802083F, -0.0747622624F, -0.0505828746F, -0.0739223063F,
            0.0854732096F, 0.0876031518F, 0.0782674327F, 0.0630588233F,
            0.0672768652F, 0.0790482089F, 0.0871619284F, -0.0709688812F,
            -0.0572536215F, 0.0798800886F, 0.0494370833F, 0.0733193532F
        }
    },
    {
        .id = "encyclopedia:cluster-005-0002:0012", .category = 8U, .observations = UINT64_C(5784),
        .description = "Geography, Nature, and Places; article-title cues include Sea, Province, Lake, Park, Mount, Storm; observed target words include Town (62), Sea (49), Province (47), Area (46)",
        .cluster = 26U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0786284506F, 0.0883478820F, -0.0679825619F, 0.0931867659F,
            0.0516719669F, -0.0620225929F, -0.0691335574F, 0.0771715418F,
            -0.0746858642F, -0.0786080584F, 0.0852403194F, 0.0660874993F,
            0.0766215846F, -0.0513051823F, -0.0651808679F, -0.0854033530F,
            -0.0564907417F, -0.0666682571F, -0.0611260422F, -0.0690112561F,
            0.0788933560F, 0.0728317946F, 0.0744514242F, 0.0646410659F,
            0.0744514540F, 0.0739526078F, 0.0851894692F, -0.0623995364F,
            -0.0672389120F, 0.0679111779F, 0.0613705404F, 0.0621040948F
        }
    },
};

static const cgai_knowledge_module module = {
    .category = {
        .index = 8U,
        .key = "encyclopedia/cluster-005",
        .name = "Geography, Nature, and Places: Sea, Province, Lake",
        .description = "Geography, Nature, and Places; article-title cues include Sea, Province, Lake, Park, Mount, Storm",
        .count = sizeof(centroids) / sizeof(centroids[0])
    },
    .rows = centroids
};

const cgai_knowledge_module *cgai_knowledge_encyclopedia_cluster_005(void) { return &module; }
