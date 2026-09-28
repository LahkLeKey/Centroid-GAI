/* Curated static knowledge baseline; maintain these C sources directly. */
/* Category: Science and Mathematics: Mathematics, Theory, Characters; centroids: 3. */
#include "../knowledge_catalog.h"

static const cgai_static_knowledge_centroid centroids[] = {
    {
        .id = "encyclopedia:cluster-018-0002:0003", .category = 21U, .observations = UINT64_C(5487),
        .description = "Science and Mathematics; article-title cues include Mathematics, Theory, Characters, Computer, Championship, Language; observed target words include Common (37), United (37)",
        .cluster = 63U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0765377432F, 0.0925175697F, -0.0674952641F, 0.0904125348F,
            0.0545869432F, -0.0657126606F, -0.0607834309F, 0.0700730607F,
            -0.0703305155F, -0.0682471842F, 0.0888233930F, 0.0717051104F,
            0.0766988248F, -0.0586033762F, -0.0742074698F, -0.0823906362F,
            -0.0551238358F, -0.0661957040F, -0.0666041821F, -0.0693532974F,
            0.0765915066F, 0.0828523710F, 0.0765161142F, 0.0658953264F,
            0.0754528865F, 0.0763873607F, 0.0868688002F, -0.0595484786F,
            -0.0696970075F, 0.0752382576F, 0.0646387637F, 0.0631137863F
        }
    },
    {
        .id = "encyclopedia:cluster-018-0003:0015", .category = 21U, .observations = UINT64_C(5558),
        .description = "Science and Mathematics; article-title cues include Mathematics, Theory, Characters, Computer, Championship, Language",
        .cluster = 64U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            0.0238225423F, 0.0351242013F, -0.0114818839F, 0.0353680700F,
            -0.0213204883F, 0.0343714990F, 0.0447295941F, 0.0101566501F,
            -0.0100506386F, -0.0260065868F, 0.0347636864F, 0.0471785516F,
            -0.0367039107F, 0.0541864485F, -0.0475390516F, -0.0452384315F,
            -0.00205675699F, -0.0453445651F, 0.0133266263F, -0.0227623805F,
            0.0226139221F, 0.0342017896F, 0.00688065635F, -0.00397571968F,
            -0.0499245264F, 0.0581304319F, 0.0283177607F, 0.00160090427F,
            0.0505076349F, 0.0284450185F, 0.00609609485F, -0.00642477209F
        }
    },
    {
        .id = "encyclopedia:cluster-018-0004:0002", .category = 21U, .observations = UINT64_C(7087),
        .description = "Science and Mathematics; article-title cues include Mathematics, Theory, Characters, Computer, Championship, Language",
        .cluster = 65U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0211273767F, 0.0816078708F, -0.0206451472F, 0.0783900842F,
            -0.0802442059F, 0.0138687724F, -0.0679634288F, 0.0206119213F,
            -0.0734012499F, -0.0221750475F, 0.0817326754F, -0.0687867776F,
            0.0660927072F, 0.0128709842F, 0.0624842383F, -0.0787726417F,
            0.0709816888F, 0.0731519014F, 0.0182588398F, 0.0764276013F,
            0.0663839579F, -0.0126298759F, 0.0754217729F, -0.0725448281F,
            0.0750392228F, -0.0137190782F, -0.0107008861F, 0.0755049214F,
            -0.0738753751F, 0.0177184232F, -0.0157728121F, -0.0809259564F
        }
    },
};

static const cgai_knowledge_module module = {
    .category = {
        .index = 21U,
        .key = "encyclopedia/cluster-018",
        .name = "Science and Mathematics: Mathematics, Theory, Characters",
        .description = "Science and Mathematics; article-title cues include Mathematics, Theory, Characters, Computer, Championship, Language",
        .count = sizeof(centroids) / sizeof(centroids[0])
    },
    .rows = centroids
};

const cgai_knowledge_module *cgai_knowledge_encyclopedia_cluster_018(void) { return &module; }
