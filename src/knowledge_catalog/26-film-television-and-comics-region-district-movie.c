/* Curated static knowledge baseline; maintain these C sources directly. */
/* Category: Film, Television, and Comics: Region, District, Movie; centroids: 3. */
#include "../knowledge_catalog.h"

static const cgai_static_knowledge_centroid centroids[] = {
    {
        .id = "encyclopedia:cluster-023-0000:0005", .category = 26U, .observations = UINT64_C(4307),
        .description = "Film, Television, and Comics; article-title cues include Region, District, Movie, Angeles, Apple, Carlo; observed target words include References (59)",
        .cluster = 78U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0805284306F, 0.0779837817F, -0.0692822263F, 0.0732773691F,
            -0.0799127817F, 0.0668059960F, -0.0746727586F, 0.0661628172F,
            -0.0633992478F, -0.0758766234F, 0.0772448033F, -0.0729490221F,
            0.0764103234F, 0.0606494062F, 0.0611966811F, -0.0709378198F,
            0.0580635816F, 0.0746728107F, 0.0721417069F, 0.0764514431F,
            0.0643844157F, -0.0718680844F, 0.0800495669F, -0.0717175528F,
            0.0668470412F, -0.0632078871F, -0.0628931969F, 0.0723881796F,
            -0.0678457990F, 0.0686394200F, -0.0584467053F, -0.0731814280F
        }
    },
    {
        .id = "encyclopedia:cluster-023-0000:0008", .category = 26U, .observations = UINT64_C(4338),
        .description = "Film, Television, and Comics; article-title cues include Region, District, Movie, Angeles, Apple, Carlo",
        .cluster = 79U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            0.00707704760F, 0.0677550137F, -0.0443911739F, 0.0675917119F,
            0.000787849771F, -0.0274931379F, 0.0222634934F, -0.0533970296F,
            0.0301827248F, 0.000801432878F, -0.0677685514F, 0.0594689623F,
            -0.0576079302F, 0.00273029599F, 0.0408186503F, -0.0162459668F,
            -0.00584094878F, -0.0299925208F, 0.0563582517F, -0.0322610028F,
            -0.0471078306F, 0.0490638427F, -0.0616966337F, 0.0503271334F,
            -0.0668992102F, 0.0735279024F, 0.0460482836F, 0.0629868880F,
            -0.00657445006F, -0.0466460250F, 0.0513052195F, -0.0806456432F
        }
    },
    {
        .id = "encyclopedia:cluster-023-0001:0015", .category = 26U, .observations = UINT64_C(6106),
        .description = "Film, Television, and Comics; article-title cues include Region, District, Movie, Angeles, Apple, Carlo; observed target words include References (103)",
        .cluster = 80U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.00192042021F, 0.0863039047F, -0.000443916972F, 0.0712973475F,
            -0.0786703229F, -0.000993994647F, -0.0802338421F, -0.00807741191F,
            -0.0709886551F, -0.00192043395F, 0.0859660506F, -0.0838912502F,
            0.0766824633F, -0.00923548080F, 0.0677171722F, -0.0774831772F,
            0.0669546276F, 0.0774350613F, 0.00264422246F, 0.0775798187F,
            0.0808710232F, -0.00149583200F, 0.0786413774F, -0.0816621184F,
            0.0752444267F, 0.00662983535F, 0.00900385436F, 0.0781298280F,
            -0.0731215402F, -0.00647544209F, 0.0101522589F, -0.0800599158F
        }
    },
};

static const cgai_knowledge_module module = {
    .category = {
        .index = 26U,
        .key = "encyclopedia/cluster-023",
        .name = "Film, Television, and Comics: Region, District, Movie",
        .description = "Film, Television, and Comics; article-title cues include Region, District, Movie, Angeles, Apple, Carlo",
        .count = sizeof(centroids) / sizeof(centroids[0])
    },
    .rows = centroids
};

const cgai_knowledge_module *cgai_knowledge_encyclopedia_cluster_023(void) { return &module; }
