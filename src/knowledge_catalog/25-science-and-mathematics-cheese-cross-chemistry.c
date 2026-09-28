/* Curated static knowledge baseline; maintain these C sources directly. */
/* Category: Science and Mathematics: Cheese, Cross, Chemistry; centroids: 3. */
#include "../knowledge_catalog.h"

static const cgai_static_knowledge_centroid centroids[] = {
    {
        .id = "encyclopedia:cluster-022-0000:0010", .category = 25U, .observations = UINT64_C(3852),
        .description = "Science and Mathematics; article-title cues include Cheese, Cross, Chemistry, Education, Language, University",
        .cluster = 75U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0785216615F, 0.0870881006F, -0.0646009147F, 0.0954710245F,
            0.0582831204F, -0.0691442564F, -0.0676910132F, 0.0706892535F,
            -0.0612201393F, -0.0678438544F, 0.0807549655F, 0.0588644519F,
            0.0766705945F, -0.0612049066F, -0.0675532967F, -0.0796842426F,
            -0.0598280430F, -0.0618167669F, -0.0660693645F, -0.0684710965F,
            0.0687005147F, 0.0815808550F, 0.0747583807F, 0.0702302754F,
            0.0742229298F, 0.0718670562F, 0.0789346695F, -0.0605776981F,
            -0.0741312280F, 0.0689760223F, 0.0667884871F, 0.0608836338F
        }
    },
    {
        .id = "encyclopedia:cluster-022-0001:0004", .category = 25U, .observations = UINT64_C(4191),
        .description = "Science and Mathematics; article-title cues include Cheese, Cross, Chemistry, Education, Language, University",
        .cluster = 76U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            0.0145943044F, 0.0452170335F, -0.0655619279F, 0.0526829399F,
            0.0260110144F, -0.0405631438F, 0.0346298255F, -0.0677974224F,
            0.0508973375F, 0.0334909409F, -0.0706374869F, 0.0579133220F,
            -0.0435016379F, -0.0102638118F, 0.0418988653F, -0.0383136161F,
            0.0206963588F, 0.00676287478F, 0.0656744763F, 0.00289635849F,
            -0.0467496105F, 0.0480149761F, -0.0419832207F, 0.0639027059F,
            -0.0594035685F, 0.0622717142F, 0.0338987447F, 0.0829400942F,
            -0.00192621909F, -0.0564087965F, 0.0413083211F, -0.0738854483F
        }
    },
    {
        .id = "encyclopedia:cluster-022-0001:0011", .category = 25U, .observations = UINT64_C(4603),
        .description = "Science and Mathematics; article-title cues include Cheese, Cross, Chemistry, Education, Language, University; observed target words include Century (32)",
        .cluster = 77U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0729175806F, 0.0833381787F, -0.0571589060F, 0.0778590441F,
            0.0447542779F, -0.0645582303F, -0.0656976402F, 0.0699860752F,
            -0.0608458072F, -0.0666320473F, 0.0774621814F, 0.0553283691F,
            0.0708694160F, -0.0537665486F, -0.0690643415F, -0.0816994235F,
            -0.0563011803F, -0.0554948300F, -0.0638798550F, -0.0594631992F,
            0.0671057850F, 0.0639437735F, 0.0728535429F, 0.0633550063F,
            0.0669903457F, 0.0733144656F, 0.0781533197F, -0.0541761890F,
            -0.0631245375F, 0.0656206608F, 0.0643790737F, 0.0550722852F
        }
    },
};

static const cgai_knowledge_module module = {
    .category = {
        .index = 25U,
        .key = "encyclopedia/cluster-022",
        .name = "Science and Mathematics: Cheese, Cross, Chemistry",
        .description = "Science and Mathematics; article-title cues include Cheese, Cross, Chemistry, Education, Language, University",
        .count = sizeof(centroids) / sizeof(centroids[0])
    },
    .rows = centroids
};

const cgai_knowledge_module *cgai_knowledge_encyclopedia_cluster_022(void) { return &module; }
