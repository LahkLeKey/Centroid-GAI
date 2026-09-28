/* Curated static knowledge baseline; maintain these C sources directly. */
/* Category: Music and Performing Arts: Song, Love, Woman; centroids: 3. */
#include "../knowledge_catalog.h"

static const cgai_static_knowledge_centroid centroids[] = {
    {
        .id = "encyclopedia:cluster-011-0000:0007", .category = 14U, .observations = UINT64_C(4168),
        .description = "Music and Performing Arts; article-title cues include Song, Love, Woman, Bridge, Don, Night; observed target words include Song (178), United (52), Single (51), Band (37)",
        .cluster = 42U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0818283260F, 0.0896749198F, -0.0728935897F, 0.103826582F,
            0.0649198890F, -0.0616117828F, -0.0640857965F, 0.0759895518F,
            -0.0767813772F, -0.0720594972F, 0.0869039670F, 0.0712535754F,
            0.0809238255F, -0.0622621104F, -0.0829736516F, -0.0783931017F,
            -0.0672242567F, -0.0718332455F, -0.0743213370F, -0.0623185523F,
            0.0845711678F, 0.0768096298F, 0.0858013034F, 0.0658530071F,
            0.0716918334F, 0.0779971704F, 0.0944956914F, -0.0545570105F,
            -0.0741518363F, 0.0672384351F, 0.0586145259F, 0.0625305548F
        }
    },
    {
        .id = "encyclopedia:cluster-011-0001:0010", .category = 14U, .observations = UINT64_C(3998),
        .description = "Music and Performing Arts; article-title cues include Song, Love, Woman, Bridge, Don, Night; observed target words include Song (106), Number (66), Songs (42)",
        .cluster = 43U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0626545548F, 0.0597656853F, 0.0632439777F, 0.0262497105F,
            -0.0122626480F, 0.0628460869F, -0.0582033582F, -0.0636124387F,
            -0.0624185987F, -0.0280920994F, 0.0220933929F, -0.0270161442F,
            0.0646736473F, -0.0100960694F, -0.0520131178F, 0.0349455290F,
            0.0635092482F, 0.0687120855F, -0.00122332794F, 0.0745486468F,
            -0.0526026152F, -0.0471640378F, 0.0594709143F, 0.00275614625F,
            -0.0505538993F, 0.00113488222F, 0.0584981218F, 0.00465745432F,
            0.0376427732F, -0.00551228551F, -0.0457343496F, -0.0616669878F
        }
    },
    {
        .id = "encyclopedia:cluster-011-0001:0015", .category = 14U, .observations = UINT64_C(4184),
        .description = "Music and Performing Arts; article-title cues include Song, Love, Woman, Bridge, Don, Night; observed target words include Number (148), Songs (54)",
        .cluster = 44U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            0.0481375866F, 0.0634745285F, 0.0518838242F, 0.0612211600F,
            -0.0479263365F, -0.0662349090F, -0.0495599732F, -0.0478136614F,
            -0.0452926867F, 0.0583059080F, 0.0786143392F, -0.0532358140F,
            0.0673052445F, -0.0451377966F, 0.0351244025F, -0.0715725273F,
            0.0132385204F, 0.0561792403F, -0.0674600825F, 0.0519542135F,
            0.0461658277F, 0.0455603004F, 0.0527569391F, -0.0556582138F,
            0.0621084645F, 0.0807833523F, 0.0540104024F, 0.0537428632F,
            -0.0564468130F, -0.0602916703F, 0.0776003078F, -0.0682629645F
        }
    },
};

static const cgai_knowledge_module module = {
    .category = {
        .index = 14U,
        .key = "encyclopedia/cluster-011",
        .name = "Music and Performing Arts: Song, Love, Woman",
        .description = "Music and Performing Arts; article-title cues include Song, Love, Woman, Bridge, Don, Night",
        .count = sizeof(centroids) / sizeof(centroids[0])
    },
    .rows = centroids
};

const cgai_knowledge_module *cgai_knowledge_encyclopedia_cluster_011(void) { return &module; }
