/* Curated static knowledge baseline; maintain these C sources directly. */
/* Category: Sports and Competitions: Island, Women, George; centroids: 3. */
#include "../knowledge_catalog.h"

static const cgai_static_knowledge_centroid centroids[] = {
    {
        .id = "encyclopedia:cluster-028-0002:0007", .category = 31U, .observations = UINT64_C(5248),
        .description = "Sports and Competitions; article-title cues include Island, Women, George, Cup, Afc, Championship; observed target words include References (74)",
        .cluster = 93U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0717146248F, 0.0816400573F, -0.0825047567F, 0.0654603764F,
            -0.0650675446F, 0.0660889596F, -0.0750829130F, 0.0650336891F,
            -0.0720961913F, -0.0723993331F, 0.0888262317F, -0.0747459903F,
            0.0565788858F, 0.0480230339F, 0.0541761182F, -0.0708274096F,
            0.0747236237F, 0.0797089785F, 0.0728933662F, 0.0549732484F,
            0.0759811252F, -0.0656513646F, 0.0738702565F, -0.0745887235F,
            0.0656512678F, -0.0542322472F, -0.0503472500F, 0.0691882148F,
            -0.0713216960F, 0.0804951862F, -0.0610139705F, -0.0807419196F
        }
    },
    {
        .id = "encyclopedia:cluster-028-0002:0011", .category = 31U, .observations = UINT64_C(4908),
        .description = "Sports and Competitions; article-title cues include Island, Women, George, Cup, Afc, Championship; observed target words include University (46), Magnetic (37)",
        .cluster = 94U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0455028228F, 0.0941153392F, -0.0294747818F, 0.0913418233F,
            0.0328965038F, -0.0362701416F, -0.0302071497F, 0.0781833455F,
            -0.0738611743F, -0.0809925720F, 0.0862511918F, 0.0709915236F,
            0.0407724269F, -0.0364982486F, -0.0802242458F, -0.0832138211F,
            -0.0274937712F, -0.0750016272F, -0.0370386429F, -0.0728765652F,
            0.0841502845F, 0.0840660185F, 0.0824453086F, 0.0338089280F,
            0.0376868770F, 0.0799841434F, 0.0893967003F, -0.0689986274F,
            -0.0421052016F, 0.0801643506F, 0.0296668541F, 0.0790479183F
        }
    },
    {
        .id = "encyclopedia:cluster-028-0003:0015", .category = 31U, .observations = UINT64_C(5177),
        .description = "Sports and Competitions; article-title cues include Island, Women, George, Cup, Afc, Championship; observed target words include Age (39)",
        .cluster = 95U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0251887701F, 0.0831581056F, -0.0145236533F, 0.0888378695F,
            0.0146374665F, -0.0180521086F, -0.0145236691F, 0.0667336583F,
            -0.0746103302F, -0.0790833682F, 0.0781957060F, 0.0719466284F,
            0.0200098883F, -0.0154456273F, -0.0698979571F, -0.0749858543F,
            -0.0147399250F, -0.0668019950F, -0.0197480712F, -0.0695110336F,
            0.0801989064F, 0.0806996897F, 0.0691808388F, 0.0217968728F,
            0.0192017313F, 0.0809842274F, 0.0809955820F, -0.0675078407F,
            -0.0195887163F, 0.0655045509F, 0.0157757141F, 0.0660393760F
        }
    },
};

static const cgai_knowledge_module module = {
    .category = {
        .index = 31U,
        .key = "encyclopedia/cluster-028",
        .name = "Sports and Competitions: Island, Women, George",
        .description = "Sports and Competitions; article-title cues include Island, Women, George, Cup, Afc, Championship",
        .count = sizeof(centroids) / sizeof(centroids[0])
    },
    .rows = centroids
};

const cgai_knowledge_module *cgai_knowledge_encyclopedia_cluster_028(void) { return &module; }
