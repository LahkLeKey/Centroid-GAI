/* Curated static knowledge baseline; maintain these C sources directly. */
/* Category: History, Government, and Conflict: United, States, John; centroids: 3. */
#include "../knowledge_catalog.h"

static const cgai_static_knowledge_centroid centroids[] = {
    {
        .id = "encyclopedia:cluster-009-0000:0003", .category = 12U, .observations = UINT64_C(5360),
        .description = "History, Government, and Conflict; article-title cues include United, States, John, Election, Presidential, William; observed target words include American (218)",
        .cluster = 36U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            0.0578592420F, 0.0884982422F, 0.0800882503F, 0.0801980719F,
            -0.0770979673F, -0.0571886040F, -0.0680943653F, -0.0737667903F,
            -0.0783620551F, 0.0619598664F, 0.0874099582F, -0.0687098727F,
            0.0766471475F, -0.0679073930F, 0.0684789717F, -0.0783072710F,
            0.0627951697F, 0.0664451644F, -0.0767791420F, 0.0680390894F,
            0.0694792941F, 0.0504274964F, 0.0763396919F, -0.0672916323F,
            0.0645984113F, 0.0590245165F, 0.0638616607F, 0.0810116827F,
            -0.0636638403F, -0.0740527511F, 0.0741844922F, -0.0850132033F
        }
    },
    {
        .id = "encyclopedia:cluster-009-0000:0015", .category = 12U, .observations = UINT64_C(6466),
        .description = "History, Government, and Conflict; article-title cues include United, States, John, Election, Presidential, William; observed target words include D (92)",
        .cluster = 37U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0113549838F, 0.0310302693F, 0.00498488871F, 0.0498306714F,
            0.0314404406F, -0.0513342358F, 0.0618963614F, -0.0264007617F,
            -0.0199668743F, 0.0214796923F, -0.0503318831F, 0.0645755678F,
            0.00768236909F, -0.0104254270F, 0.0544419028F, 0.0423031375F,
            -0.00230562547F, -0.0222907364F, 0.0407357104F, -0.0208964087F,
            0.00698066084F, 0.0208600163F, -0.00606935378F, -0.0331535749F,
            -0.0508786738F, 0.0393869691F, 0.0221540239F, 0.0806879178F,
            -0.0165039077F, -0.0315223262F, -0.000911316951F, -0.0582420044F
        }
    },
    {
        .id = "encyclopedia:cluster-009-0004:0015", .category = 12U, .observations = UINT64_C(5857),
        .description = "History, Government, and Conflict; article-title cues include United, States, John, Election, Presidential, William; observed target words include United (282)",
        .cluster = 38U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0333411545F, 0.0863007605F, -0.00276669418F, 0.0846003816F,
            0.0148797706F, -0.0157852340F, -0.00547302887F, 0.0618733577F,
            -0.0731113032F, -0.0705455542F, 0.0811396018F, 0.0645595789F,
            0.0127670271F, -0.0156242698F, -0.0733827576F, -0.0657064170F,
            -0.0133203948F, -0.0637244880F, -0.0233810507F, -0.0677388534F,
            0.0781013668F, 0.0769946426F, 0.0615514331F, 0.0196787771F,
            0.0230994094F, 0.0729702860F, 0.0818639845F, -0.0695998743F,
            -0.0179885533F, 0.0615715533F, 0.0111070285F, 0.0656159669F
        }
    },
};

static const cgai_knowledge_module module = {
    .category = {
        .index = 12U,
        .key = "encyclopedia/cluster-009",
        .name = "History, Government, and Conflict: United, States, John",
        .description = "History, Government, and Conflict; article-title cues include United, States, John, Election, Presidential, William",
        .count = sizeof(centroids) / sizeof(centroids[0])
    },
    .rows = centroids
};

const cgai_knowledge_module *cgai_knowledge_encyclopedia_cluster_009(void) { return &module; }
