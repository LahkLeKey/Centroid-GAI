/* Curated static knowledge baseline; maintain these C sources directly. */
/* Category: Video Games and Fantasy: Game, Video, Games; centroids: 3. */
#include "../knowledge_catalog.h"

static const cgai_static_knowledge_centroid centroids[] = {
    {
        .id = "encyclopedia:cluster-007-0000:0013", .category = 10U, .observations = UINT64_C(5184),
        .description = "Video Games and Fantasy; article-title cues include Game, Video, Games, Mario, Super, Series",
        .cluster = 30U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.00686555076F, 0.0421140492F, 0.00436486118F, 0.0514688939F,
            -0.0273371860F, 0.00491046719F, -0.00202328945F, -0.0324636400F,
            0.00210285885F, -0.0373513252F, -0.0241999794F, 0.0309291389F,
            -0.0346347168F, 0.0164818764F, 0.0294286646F, 0.0103779053F,
            -0.0384539440F, -0.0460582748F, 0.00677461037F, -0.0248023886F,
            -0.0586186983F, 0.0154020134F, -0.0532421470F, 0.0301333964F,
            -0.0501390174F, 0.0551972613F, 0.0484226309F, 0.0472178273F,
            0.0274395142F, 0.00134128821F, 0.0620742030F, -0.0597779676F
        }
    },
    {
        .id = "encyclopedia:cluster-007-0001:0004", .category = 10U, .observations = UINT64_C(7459),
        .description = "Video Games and Fantasy; article-title cues include Game, Video, Games, Mario, Super, Series; observed target words include Game (200), Player (74)",
        .cluster = 31U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0211955328F, 0.0842763260F, -0.00146939151F, 0.0884635150F,
            -0.00891901460F, -0.0627886727F, -0.0658854693F, 0.00874521490F,
            -0.0675838143F, -0.00470045675F, 0.0847189426F, -0.00183278346F,
            0.0726318955F, -0.0618248880F, -0.0176168736F, -0.0814717561F,
            -0.00179328688F, -0.00334957498F, -0.0689031184F, 0.000229096462F,
            0.0710916370F, 0.0703251362F, 0.0739435181F, 0.00216457574F,
            0.0748676434F, 0.0697089732F, 0.0807689950F, 0.0000711009488F,
            -0.0693376958F, 0.0133666983F, 0.0692271814F, -0.00235417834F
        }
    },
    {
        .id = "encyclopedia:cluster-007-0001:0014", .category = 10U, .observations = UINT64_C(5797),
        .description = "Video Games and Fantasy; article-title cues include Game, Video, Games, Mario, Super, Series",
        .cluster = 32U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0209191982F, -0.0287563261F, -0.0607348382F, -0.0324258469F,
            -0.0533349290F, 0.0215088110F, -0.00970742851F, 0.0301692393F,
            0.0458434187F, 0.0387991481F, -0.0332797058F, -0.0281464290F,
            -0.0362680927F, 0.0472258590F, -0.0260423049F, -0.0299557503F,
            0.0247615557F, -0.0289392546F, 0.0262354650F, -0.0300370976F,
            -0.0286750048F, 0.0249140188F, -0.0208684094F, 0.0255238470F,
            0.00824369024F, -0.0418182164F, -0.00422857050F, -0.00842666067F,
            0.0111914938F, -0.0300777145F, -0.00748131610F, 0.00643433956F
        }
    },
};

static const cgai_knowledge_module module = {
    .category = {
        .index = 10U,
        .key = "encyclopedia/cluster-007",
        .name = "Video Games and Fantasy: Game, Video, Games",
        .description = "Video Games and Fantasy; article-title cues include Game, Video, Games, Mario, Super, Series",
        .count = sizeof(centroids) / sizeof(centroids[0])
    },
    .rows = centroids
};

const cgai_knowledge_module *cgai_knowledge_encyclopedia_cluster_007(void) { return &module; }
