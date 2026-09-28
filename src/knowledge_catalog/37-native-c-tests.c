/* Curated static knowledge baseline; maintain these C sources directly. */
/* Category: Native C Tests; centroids: 3. */
#include "../knowledge_catalog.h"

static const cgai_static_knowledge_centroid centroids[] = {
    {
        .id = "native-tests-000:0001", .category = 37U, .observations = UINT64_C(1922),
        .description = "Native C tests and correctness fixtures; observed target words include Test (90), Check (54)",
        .cluster = 111U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0529164784F, -0.0361463167F, -0.0240055695F, -0.0114049539F,
            0.0179351885F, -0.0376485772F, 0.00932018086F, -0.0629724637F,
            0.0180271715F, 0.0405304842F, -0.0410823375F, -0.0206024870F,
            0.0513835475F, 0.0397946537F, -0.0275313091F, 0.0163409617F,
            0.0455891266F, -0.0504332073F, 0.0523646660F, -0.0677551627F,
            -0.0457117856F, 0.0779338107F, -0.0350119807F, -0.00432284130F,
            0.0169541370F, -0.0524566285F, -0.0113129681F, 0.0397640131F,
            0.0444548130F, 0.0106998067F, 0.00597839663F, -0.0714035556F
        }
    },
    {
        .id = "native-tests-000:0012", .category = 37U, .observations = UINT64_C(1869),
        .description = "Native C tests and correctness fixtures; observed target words include Model (147), Check (84), Abi (60), Error (41)",
        .cluster = 112U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            0.109117866F, 0.112239167F, -0.0560564958F, -0.0497194119F,
            0.0881519392F, -0.101078384F, -0.0349643826F, 0.0680370778F,
            0.0586102754F, -0.0856927484F, 0.0817517191F, -0.107446872F,
            -0.0720096529F, 0.0533766262F, -0.0726402774F, -0.0981461033F,
            -0.0809319392F, 0.0570969172F, 0.100258581F, -0.0666498020F,
            0.0890030712F, -0.0329465829F, 0.112838261F, -0.112081505F,
            -0.0750994235F, 0.0527776033F, -0.0864810273F, -0.0757929683F,
            -0.0903588012F, 0.0976102352F, 0.0803329647F, 0.104514830F
        }
    },
    {
        .id = "native-tests-000:0013", .category = 37U, .observations = UINT64_C(1795),
        .description = "Native C tests and correctness fixtures; observed target words include Cgai (110), Null (45)",
        .cluster = 113U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            0.103144281F, 0.0763570145F, 0.0789832398F, 0.100616716F,
            0.0180880018F, -0.0855159163F, -0.0287898201F, -0.102848999F,
            -0.0621427000F, -0.00213379459F, 0.0525898598F, -0.109151721F,
            0.0885032117F, -0.0977277905F, 0.107083701F, -0.0581049062F,
            0.0320069194F, -0.00945434812F, -0.0186132509F, 0.0421178192F,
            0.0744858459F, 0.0423147790F, 0.0784252062F, -0.0779328570F,
            -0.0119492505F, -0.0235373881F, 0.0634557605F, 0.0794756860F,
            -0.0339109227F, -0.0335826464F, 0.0351255685F, -0.0638168976F
        }
    },
};

static const cgai_knowledge_module module = {
    .category = {
        .index = 37U,
        .key = "native-tests",
        .name = "Native C Tests",
        .description = "Native C tests and correctness fixtures",
        .count = sizeof(centroids) / sizeof(centroids[0])
    },
    .rows = centroids
};

const cgai_knowledge_module *cgai_knowledge_native_tests(void) { return &module; }
