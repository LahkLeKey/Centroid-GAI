/* Curated static knowledge baseline; maintain these C sources directly. */
/* Category: Geography, Nature, and Places: County, York, South; centroids: 3. */
#include "../knowledge_catalog.h"

static const cgai_static_knowledge_centroid centroids[] = {
    {
        .id = "encyclopedia:cluster-029-0000:0007", .category = 32U, .observations = UINT64_C(4369),
        .description = "Geography, Nature, and Places; article-title cues include County, York, South, Alabama, Carolina, Wisconsin; observed target words include Li (401), County (60)",
        .cluster = 96U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            0.00404615654F, 0.0380338877F, 0.0145122223F, 0.0215255748F,
            0.0280399602F, -0.0200015083F, 0.0616094582F, 0.0593031645F,
            -0.0417699143F, -0.0792237893F, 0.0361591130F, 0.0533553064F,
            -0.0556751266F, 0.0449662581F, -0.0917263627F, -0.0310475677F,
            0.0426735058F, -0.0419856124F, 0.0846591219F, -0.0791292936F,
            0.0277162194F, 0.0275813211F, 0.0223213062F, -0.0727904290F,
            -0.0573071688F, 0.0166836642F, 0.0168455224F, -0.0482167043F,
            0.0448854379F, 0.0811659247F, 0.00268396921F, 0.00891503692F
        }
    },
    {
        .id = "encyclopedia:cluster-029-0000:0011", .category = 32U, .observations = UINT64_C(5973),
        .description = "Geography, Nature, and Places; article-title cues include County, York, South, Alabama, Carolina, Wisconsin; observed target words include Ohio (996), County (44)",
        .cluster = 97U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            0.0662653670F, 0.0774526522F, 0.103181332F, 0.0473733172F,
            -0.0799979568F, -0.106446773F, -0.0391949564F, -0.0503033213F,
            -0.0996396914F, 0.0738814101F, 0.0790113434F, -0.100685425F,
            0.0477283932F, -0.0685442686F, 0.0458243974F, -0.109722167F,
            0.0485175811F, 0.0726680905F, -0.0660977066F, 0.0758346319F,
            0.0765153170F, 0.100912355F, 0.108804524F, -0.0519408956F,
            0.0683863014F, 0.0734866634F, 0.0757656917F, 0.0790705457F,
            -0.101415530F, -0.111852996F, 0.0748383999F, -0.106259145F
        }
    },
    {
        .id = "encyclopedia:cluster-029-0000:0015", .category = 32U, .observations = UINT64_C(9389),
        .description = "Geography, Nature, and Places; article-title cues include County, York, South, Alabama, Carolina, Wisconsin; observed target words include County (545), Align (277)",
        .cluster = 98U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            0.103177719F, -0.0754002258F, 0.0935755968F, -0.0808413923F,
            0.0688106418F, -0.133723050F, 0.114493310F, -0.0451058075F,
            -0.0982635990F, -0.0455137305F, -0.0598104745F, -0.0675801337F,
            -0.0916490704F, 0.0332691595F, -0.0804147497F, -0.100472867F,
            0.0104684001F, 0.0609463900F, 0.0886109993F, 0.0604821406F,
            0.0287504438F, 0.0725570396F, 0.0801386908F, 0.0758896321F,
            -0.0863768160F, -0.0639589205F, -0.0567730069F, 0.0771448091F,
            -0.0940963477F, -0.0667707473F, 0.0723625049F, -0.113671251F
        }
    },
};

static const cgai_knowledge_module module = {
    .category = {
        .index = 32U,
        .key = "encyclopedia/cluster-029",
        .name = "Geography, Nature, and Places: County, York, South",
        .description = "Geography, Nature, and Places; article-title cues include County, York, South, Alabama, Carolina, Wisconsin",
        .count = sizeof(centroids) / sizeof(centroids[0])
    },
    .rows = centroids
};

const cgai_knowledge_module *cgai_knowledge_encyclopedia_cluster_029(void) { return &module; }
