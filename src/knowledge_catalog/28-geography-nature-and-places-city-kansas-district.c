/* Curated static knowledge baseline; maintain these C sources directly. */
/* Category: Geography, Nature, and Places: City, Kansas, District; centroids: 3. */
#include "../knowledge_catalog.h"

static const cgai_static_knowledge_centroid centroids[] = {
    {
        .id = "encyclopedia:cluster-025-0000:0012", .category = 28U, .observations = UINT64_C(4732),
        .description = "Geography, Nature, and Places; article-title cues include City, Kansas, District, California, Governorate, Chicago; observed target words include City (205), Capital (88), State (52), Name (41)",
        .cluster = 84U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0837559551F, 0.103742301F, -0.0684393346F, 0.0930954888F,
            0.0519770719F, -0.0648156628F, -0.0719010532F, 0.0828718916F,
            -0.0812530965F, -0.0792731121F, 0.0931826681F, 0.0740428716F,
            0.0777167305F, -0.0600463003F, -0.0784511268F, -0.0927093327F,
            -0.0585395768F, -0.0760604292F, -0.0638941005F, -0.0731712654F,
            0.0860348269F, 0.0934939757F, 0.0878902674F, 0.0756867677F,
            0.0829839334F, 0.0783515796F, 0.0930332318F, -0.0645541698F,
            -0.0776043534F, 0.0754750371F, 0.0635952950F, 0.0751760900F
        }
    },
    {
        .id = "encyclopedia:cluster-025-0000:0014", .category = 28U, .observations = UINT64_C(4688),
        .description = "Geography, Nature, and Places; article-title cues include City, Kansas, District, California, Governorate, Chicago",
        .cluster = 85U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0591141209F, 0.0350436419F, 0.0496493205F, -0.0468337871F,
            0.0516478606F, -0.0312853418F, 0.0414288566F, -0.0464063808F,
            0.0255662482F, 0.0293747708F, 0.0428618342F, 0.0122552179F,
            0.0647075772F, -0.0581461936F, 0.0122929197F, 0.0392921343F,
            -0.0357097760F, 0.0645819157F, -0.0510445349F, 0.0701247677F,
            0.00750396214F, 0.0353075676F, -0.0168430470F, 0.0211669393F,
            0.0333467871F, -0.0572161935F, 0.0412025973F, 0.0669699609F,
            -0.0133361723F, -0.0460795723F, -0.0650469810F, 0.0154604092F
        }
    },
    {
        .id = "encyclopedia:cluster-025-0002:0014", .category = 28U, .observations = UINT64_C(4559),
        .description = "Geography, Nature, and Places; article-title cues include City, Kansas, District, California, Governorate, Chicago; observed target words include References (132), Cities (52)",
        .cluster = 86U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0585378185F, 0.0782484859F, -0.0707521066F, 0.0610065013F,
            -0.0789851919F, 0.0681671426F, -0.0684772730F, 0.0640569180F,
            -0.0652459264F, -0.0716439337F, 0.0818030834F, -0.0831731036F,
            0.0686711147F, 0.0507697649F, 0.0591840893F, -0.0701704621F,
            0.0631908104F, 0.0734404996F, 0.0875029117F, 0.0676759481F,
            0.0751077607F, -0.0774601549F, 0.0742935911F, -0.0799417719F,
            0.0665772483F, -0.0692785382F, -0.0565991104F, 0.0652200878F,
            -0.0592874624F, 0.0674043670F, -0.0527602658F, -0.0677663013F
        }
    },
};

static const cgai_knowledge_module module = {
    .category = {
        .index = 28U,
        .key = "encyclopedia/cluster-025",
        .name = "Geography, Nature, and Places: City, Kansas, District",
        .description = "Geography, Nature, and Places; article-title cues include City, Kansas, District, California, Governorate, Chicago",
        .count = sizeof(centroids) / sizeof(centroids[0])
    },
    .rows = centroids
};

const cgai_knowledge_module *cgai_knowledge_encyclopedia_cluster_025(void) { return &module; }
