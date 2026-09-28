/* Curated static knowledge baseline; maintain these C sources directly. */
/* Category: Native Language Bridge; centroids: 3. */
#include "../knowledge_catalog.h"

static const cgai_static_knowledge_centroid centroids[] = {
    {
        .id = "native-bridge-000:0001", .category = 35U, .observations = UINT64_C(1333),
        .description = "JavaScript to native C bindings and data conversion; observed target words include Env (118), Napi (66), Cgai (21), Model (20)",
        .cluster = 105U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            0.0201133788F, 0.0853161067F, -0.0766960606F, 0.0604285561F,
            0.0508802235F, -0.0519853570F, 0.0245781150F, -0.00198923517F,
            0.0682528466F, 0.0842993334F, -0.00265231472F, 0.0579972267F,
            -0.0205554198F, -0.0376628712F, 0.0183451548F, -0.0435863622F,
            -0.0312530957F, 0.0423927866F, 0.102423504F, 0.0363366827F,
            -0.0493772700F, 0.0629924759F, -0.0846972540F, 0.0448240936F,
            -0.0139246481F, 0.0353641510F, -0.00587929832F, 0.0620199032F,
            -0.00972515531F, -0.0400941484F, 0.0517643429F, -0.102379270F
        }
    },
    {
        .id = "native-bridge-000:0007", .category = 35U, .observations = UINT64_C(1542),
        .description = "JavaScript to native C bindings and data conversion; observed target words include Value (91), Node (69), Abi (69), Env (46)",
        .cluster = 106U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            0.0984002948F, 0.0847961754F, -0.0740199387F, -0.0795991793F,
            0.0512445904F, -0.0864012316F, -0.0603394732F, 0.0799812302F,
            0.0615623184F, -0.0661861524F, 0.0874711946F, -0.125302643F,
            -0.0852548406F, 0.0767713562F, -0.0685936436F, -0.0822741389F,
            -0.0578937791F, 0.0351948328F, 0.111049041F, -0.0799048766F,
            0.0593841374F, -0.0501364246F, 0.107189462F, -0.0773447081F,
            -0.0789113864F, 0.0847962871F, -0.0687846616F, -0.0851020068F,
            -0.122704178F, 0.0880444944F, 0.0906046852F, 0.116437279F
        }
    },
    {
        .id = "native-bridge-000:0015", .category = 35U, .observations = UINT64_C(2376),
        .description = "JavaScript to native C bindings and data conversion; observed target words include Return (63), Napi (55), Borrowed (45)",
        .cluster = 107U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0228162799F, -0.0360844843F, -0.0330836512F, 0.0262387376F,
            -0.0195426475F, -0.0525270961F, 0.00146321731F, -0.0105649317F,
            -0.0124993669F, 0.0510638729F, -0.0753186941F, -0.0398293473F,
            0.0329844393F, 0.0219979063F, -0.0299588069F, 0.0327612273F,
            0.0418381430F, -0.0484846383F, 0.0201874599F, -0.0566191226F,
            -0.0310251787F, 0.0906948298F, -0.0171122309F, 0.00974654220F,
            -0.0121521503F, -0.0251227133F, 0.0133921579F, 0.00964732561F,
            0.0177570451F, -0.00622487953F, -0.0249986984F, -0.0249491297F
        }
    },
};

static const cgai_knowledge_module module = {
    .category = {
        .index = 35U,
        .key = "native-bridge",
        .name = "Native Language Bridge",
        .description = "JavaScript to native C bindings and data conversion",
        .count = sizeof(centroids) / sizeof(centroids[0])
    },
    .rows = centroids
};

const cgai_knowledge_module *cgai_knowledge_native_bridge(void) { return &module; }
