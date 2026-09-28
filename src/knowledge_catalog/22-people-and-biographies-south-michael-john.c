/* Curated static knowledge baseline; maintain these C sources directly. */
/* Category: People and Biographies: South, Michael, John; centroids: 3. */
#include "../knowledge_catalog.h"

static const cgai_static_knowledge_centroid centroids[] = {
    {
        .id = "encyclopedia:cluster-019-0000:0015", .category = 22U, .observations = UINT64_C(7340),
        .description = "People and Biographies; article-title cues include South, Michael, John, Assembly, Tree, David; observed target words include American (669), Actor (161), English (160), German (159)",
        .cluster = 66U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            0.0572717488F, 0.0936225057F, 0.0939197093F, 0.0828810781F,
            -0.0822307914F, -0.0506085604F, -0.0650828853F, -0.0912062377F,
            -0.0898575634F, 0.0589657947F, 0.0874972120F, -0.0625380948F,
            0.0812112316F, -0.0787627697F, 0.0840371847F, -0.0876818746F,
            0.0627950951F, 0.0636058375F, -0.0853938907F, 0.0581308566F,
            0.0648100451F, 0.0468032658F, 0.0800393447F, -0.0634854808F,
            0.0522943735F, 0.0609085001F, 0.0595516935F, 0.0873768851F,
            -0.0536190458F, -0.0871921554F, 0.0918002352F, -0.0815406218F
        }
    },
    {
        .id = "encyclopedia:cluster-019-0003:0009", .category = 22U, .observations = UINT64_C(6783),
        .description = "People and Biographies; article-title cues include South, Michael, John, Assembly, Tree, David; observed target words include American (106), Actor (105)",
        .cluster = 67U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            0.0616794601F, 0.0762218162F, 0.0702364594F, 0.0976445153F,
            -0.0638599396F, -0.0801744908F, -0.0588040687F, -0.0905820504F,
            -0.0884707645F, 0.0879497677F, 0.0644507110F, -0.0591687746F,
            0.0831109509F, -0.0582740717F, 0.0898781940F, -0.0698453337F,
            0.0896263123F, 0.0625741482F, -0.0625655130F, 0.0579091646F,
            0.0784196481F, 0.0804352537F, 0.0839711428F, -0.0894611180F,
            0.0543299839F, 0.0870983973F, 0.0631648898F, 0.0943782553F,
            -0.0632952675F, -0.0869244859F, 0.0710268244F, -0.0933705419F
        }
    },
    {
        .id = "encyclopedia:cluster-019-0004:0005", .category = 22U, .observations = UINT64_C(7149),
        .description = "People and Biographies; article-title cues include South, Michael, John, Assembly, Tree, David; observed target words include American (180), Politician (140), Member (68), Caused (62)",
        .cluster = 68U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            0.0688329712F, 0.0854499266F, 0.0835376829F, 0.0883924440F,
            -0.0742318481F, -0.0813369527F, -0.0728717074F, -0.0943024606F,
            -0.0842794701F, 0.0835293829F, 0.0856889412F, -0.0729708672F,
            0.0836613700F, -0.0843124315F, 0.0918212458F, -0.0777101740F,
            0.0757319629F, 0.0715119094F, -0.0822104216F, 0.0804135874F,
            0.0790288076F, 0.0783201680F, 0.0769932196F, -0.0763833225F,
            0.0628407151F, 0.0720888823F, 0.0771908760F, 0.0952007398F,
            -0.0786333010F, -0.0924229398F, 0.0779492036F, -0.0776277259F
        }
    },
};

static const cgai_knowledge_module module = {
    .category = {
        .index = 22U,
        .key = "encyclopedia/cluster-019",
        .name = "People and Biographies: South, Michael, John",
        .description = "People and Biographies; article-title cues include South, Michael, John, Assembly, Tree, David",
        .count = sizeof(centroids) / sizeof(centroids[0])
    },
    .rows = centroids
};

const cgai_knowledge_module *cgai_knowledge_encyclopedia_cluster_019(void) { return &module; }
