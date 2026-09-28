/* Curated static knowledge baseline; maintain these C sources directly. */
/* Category: Native C Core; centroids: 3. */
#include "../knowledge_catalog.h"

static const cgai_static_knowledge_centroid centroids[] = {
    {
        .id = "native-core-000:0003", .category = 36U, .observations = UINT64_C(5621),
        .description = "Centroid-GAI C model, training, inference, and serialization; observed target words include Param (213), Return (175), Step (171), Brief (105)",
        .cluster = 108U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.134561285F, -0.0602254644F, -0.117043950F, -0.0274238717F,
            -0.0633599684F, -0.0519123413F, 0.0205049999F, -0.0509793609F,
            0.0589255765F, 0.100239635F, 0.0890434384F, 0.0618922561F,
            0.129047096F, 0.0818101615F, 0.0488617644F, 0.0436621457F,
            0.0661274046F, -0.0448781848F, 0.104390778F, 0.0697544515F,
            0.0488094054F, -0.0309251342F, -0.0626469254F, -0.0812756196F,
            0.116299555F, 0.0330427848F, 0.0367957056F, 0.0971365944F,
            0.106885843F, 0.0751955211F, -0.0423203669F, -0.133240312F
        }
    },
    {
        .id = "native-core-000:0008", .category = 36U, .observations = UINT64_C(4508),
        .description = "Centroid-GAI C model, training, inference, and serialization; observed target words include Model (68), Token (47)",
        .cluster = 109U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0665853620F, 0.0237505827F, 0.00797351450F, 0.0382859372F,
            -0.000470566680F, -0.0169404428F, -0.0489913076F, 0.0158293732F,
            -0.0393446572F, -0.0302078538F, 0.0135288229F, 0.0130451806F,
            0.0677749291F, 0.000692780071F, -0.0575008094F, 0.00220905268F,
            -0.000771210238F, 0.0208618362F, -0.0311097950F, 0.0153849656F,
            -0.00288876123F, 0.0202867426F, 0.0291752126F, 0.0475927927F,
            0.00223519816F, 0.0413707457F, 0.0764933974F, -0.0353971720F,
            0.0186266657F, 0.0291359723F, -0.0000261388213F, 0.00684939092F
        }
    },
    {
        .id = "native-core-000:0011", .category = 36U, .observations = UINT64_C(3838),
        .description = "Centroid-GAI C model, training, inference, and serialization; observed target words include Cgai (111), Model (90), Size (79), Null (76)",
        .cluster = 110U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            0.0742021427F, 0.0721138418F, -0.0140788676F, 0.0691354722F,
            0.0522162430F, -0.0783013254F, 0.0909676179F, -0.0415150449F,
            0.0442632847F, 0.0528917611F, -0.0434034802F, 0.0582806505F,
            -0.0571293607F, -0.0253941994F, 0.0291864127F, -0.00366941746F,
            0.00581885781F, -0.000859777210F, 0.0972931013F, -0.00948825758F,
            -0.0600002892F, 0.0231833123F, -0.0786083415F, 0.00334699196F,
            -0.0833219439F, 0.0537975356F, 0.0343912132F, 0.0731733069F,
            -0.0552407280F, -0.00239509903F, 0.0411005430F, -0.0756299421F
        }
    },
};

static const cgai_knowledge_module module = {
    .category = {
        .index = 36U,
        .key = "native-core",
        .name = "Native C Core",
        .description = "Centroid-GAI C model, training, inference, and serialization",
        .count = sizeof(centroids) / sizeof(centroids[0])
    },
    .rows = centroids
};

const cgai_knowledge_module *cgai_knowledge_native_core(void) { return &module; }
