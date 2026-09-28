/* Curated static knowledge baseline; maintain these C sources directly. */
/* Category: Education and Institutions: University, School, Southern; centroids: 3. */
#include "../knowledge_catalog.h"

static const cgai_static_knowledge_centroid centroids[] = {
    {
        .id = "encyclopedia:cluster-013-0000:0013", .category = 16U, .observations = UINT64_C(5009),
        .description = "Education and Institutions; article-title cues include University, School, Southern, District, Flag, John; observed target words include Game (60), Ball (57), Icrc (46), Team (45)",
        .cluster = 48U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0867001489F, 0.0976877213F, -0.0653487146F, 0.0893706903F,
            0.0579726137F, -0.0698659718F, -0.0757598728F, 0.0762067512F,
            -0.0703837797F, -0.0738186911F, 0.0839358345F, 0.0696424618F,
            0.0812417492F, -0.0613842681F, -0.0722188875F, -0.0863827392F,
            -0.0677603111F, -0.0693246499F, -0.0673838109F, -0.0729952008F,
            0.0862884521F, 0.0867003426F, 0.0880530477F, 0.0706189647F,
            0.0815711692F, 0.0776066929F, 0.0885470957F, -0.0633253828F,
            -0.0784182772F, 0.0703012049F, 0.0600549430F, 0.0644663721F
        }
    },
    {
        .id = "encyclopedia:cluster-013-0001:0015", .category = 16U, .observations = UINT64_C(4333),
        .description = "Education and Institutions; article-title cues include University, School, Southern, District, Flag, John",
        .cluster = 49U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            0.0535402521F, 0.0754758790F, 0.0682683438F, 0.0756390616F,
            -0.0596598871F, -0.0712192729F, -0.0625021011F, -0.0610877611F,
            -0.0554713309F, 0.0796509087F, 0.0749454349F, -0.0723888502F,
            0.0665138736F, -0.0564504452F, 0.0444151610F, -0.0717224926F,
            0.0566816181F, 0.0591839366F, -0.0737352371F, 0.0625021830F,
            0.0590480193F, 0.0604486912F, 0.0711921155F, -0.0653444007F,
            0.0632364750F, 0.0798683912F, 0.0663643926F, 0.0697370991F,
            -0.0662555620F, -0.0623526163F, 0.0739390552F, -0.0723480880F
        }
    },
    {
        .id = "encyclopedia:cluster-013-0002:0005", .category = 16U, .observations = UINT64_C(4580),
        .description = "Education and Institutions; article-title cues include University, School, Southern, District, Flag, John",
        .cluster = 50U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            0.00472176913F, 0.0321002752F, -0.0595560260F, 0.0575876236F,
            0.0105757313F, -0.0121710869F, 0.0265808571F, -0.0662591457F,
            0.0406303555F, 0.0122740045F, -0.0543196388F, 0.0556962378F,
            -0.0407460481F, 0.0172788072F, 0.0451334119F, -0.0117207803F,
            -0.00499194534F, -0.0158635899F, 0.0453263856F, -0.0249468703F,
            -0.0282662511F, 0.0617045090F, -0.0425860025F, 0.0491475612F,
            -0.0480153710F, 0.0639561117F, 0.0155162197F, 0.0727305710F,
            0.00703761447F, -0.0652556196F, 0.0103312638F, -0.0633384287F
        }
    },
};

static const cgai_knowledge_module module = {
    .category = {
        .index = 16U,
        .key = "encyclopedia/cluster-013",
        .name = "Education and Institutions: University, School, Southern",
        .description = "Education and Institutions; article-title cues include University, School, Southern, District, Flag, John",
        .count = sizeof(centroids) / sizeof(centroids[0])
    },
    .rows = centroids
};

const cgai_knowledge_module *cgai_knowledge_encyclopedia_cluster_013(void) { return &module; }
