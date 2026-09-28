/* Curated static knowledge baseline; maintain these C sources directly. */
/* Category: History, Government, and Conflict: Politician, John, Party; centroids: 3. */
#include "../knowledge_catalog.h"

static const cgai_static_knowledge_centroid centroids[] = {
    {
        .id = "encyclopedia:cluster-014-0000:0012", .category = 17U, .observations = UINT64_C(4143),
        .description = "History, Government, and Conflict; article-title cues include Politician, John, Party, Tom, Frank, Jim; observed target words include References (80)",
        .cluster = 51U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0785674602F, 0.0838440806F, -0.0700621828F, 0.0674166605F,
            -0.0614856705F, 0.0813551843F, -0.0809141845F, 0.0639320835F,
            -0.0681277364F, -0.0743147954F, 0.0794917569F, -0.0837587491F,
            0.0624670461F, 0.0688390359F, 0.0655676126F, -0.0819951147F,
            0.0864043012F, 0.0846263319F, 0.0916951224F, 0.0663640648F,
            0.0656529739F, -0.0781690627F, 0.0679002181F, -0.0785816088F,
            0.0604474284F, -0.0582856238F, -0.0563511997F, 0.0681134462F,
            -0.0900594741F, 0.0717545152F, -0.0609167479F, -0.0794206858F
        }
    },
    {
        .id = "encyclopedia:cluster-014-0000:0015", .category = 17U, .observations = UINT64_C(4014),
        .description = "History, Government, and Conflict; article-title cues include Politician, John, Party, Tom, Frank, Jim",
        .cluster = 52U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0512625761F, 0.0691133887F, 0.0510277078F, 0.0597916543F,
            -0.0320904702F, 0.0351291895F, -0.0523341857F, -0.0355403349F,
            -0.0753377527F, -0.0509984270F, 0.0414269753F, -0.0228127148F,
            0.0434821397F, 0.0192014389F, -0.0663977042F, 0.0119348550F,
            0.0485908464F, 0.0324721709F, 0.0261450969F, 0.0261450987F,
            -0.0233412180F, -0.0236641914F, 0.0382121392F, 0.0347915590F,
            -0.0524810478F, 0.0559748001F, 0.0670435429F, -0.0489137769F,
            0.0384029150F, 0.0535526276F, -0.0206694659F, -0.0397240967F
        }
    },
    {
        .id = "encyclopedia:cluster-014-0002:0015", .category = 17U, .observations = UINT64_C(6429),
        .description = "History, Government, and Conflict; article-title cues include Politician, John, Party, Tom, Frank, Jim; observed target words include American (102), Politician (92)",
        .cluster = 53U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            0.0296231993F, 0.0691726729F, 0.0763125941F, 0.0614277236F,
            -0.0327028893F, -0.0780816972F, -0.0455437861F, -0.0927006677F,
            -0.0723072737F, 0.0608320571F, 0.0857351124F, -0.0584948026F,
            0.0773666725F, -0.0702177286F, 0.0669545680F, -0.0528762341F,
            0.0425283574F, 0.0630776212F, -0.0752860457F, 0.0639850795F,
            0.0811337829F, 0.0690353736F, 0.0470194705F, -0.0685218349F,
            0.0603646003F, 0.0447097830F, 0.0754420012F, 0.0724998191F,
            -0.0603921153F, -0.0817021206F, 0.0502274185F, -0.0469644926F
        }
    },
};

static const cgai_knowledge_module module = {
    .category = {
        .index = 17U,
        .key = "encyclopedia/cluster-014",
        .name = "History, Government, and Conflict: Politician, John, Party",
        .description = "History, Government, and Conflict; article-title cues include Politician, John, Party, Tom, Frank, Jim",
        .count = sizeof(centroids) / sizeof(centroids[0])
    },
    .rows = centroids
};

const cgai_knowledge_module *cgai_knowledge_encyclopedia_cluster_014(void) { return &module; }
