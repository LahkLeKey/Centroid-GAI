/* Curated static knowledge baseline; maintain these C sources directly. */
/* Category: Video Games and Fantasy: Olympics, Church, Summer; centroids: 3. */
#include "../knowledge_catalog.h"

static const cgai_static_knowledge_centroid centroids[] = {
    {
        .id = "encyclopedia:cluster-030-0000:0008", .category = 33U, .observations = UINT64_C(4608),
        .description = "Video Games and Fantasy; article-title cues include Olympics, Church, Summer, Paralympics, Winter, Games; observed target words include References (78)",
        .cluster = 99U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0827234462F, 0.0862016529F, -0.0590534173F, 0.0691811889F,
            -0.0795266256F, 0.0786953345F, -0.0802426189F, 0.0488745272F,
            -0.0726723671F, -0.0765980110F, 0.0842324123F, -0.0807797089F,
            0.0720841363F, 0.0570585988F, 0.0510228090F, -0.0665726289F,
            0.0719817504F, 0.0804855376F, 0.0868666023F, 0.0678386241F,
            0.0622246638F, -0.0801401809F, 0.0751659796F, -0.0705878362F,
            0.0512785353F, -0.0478386097F, -0.0453578755F, 0.0545649491F,
            -0.0621480644F, 0.0784522742F, -0.0626594871F, -0.0844626427F
        }
    },
    {
        .id = "encyclopedia:cluster-030-0000:0014", .category = 33U, .observations = UINT64_C(4156),
        .description = "Video Games and Fantasy; article-title cues include Olympics, Church, Summer, Paralympics, Winter, Games; observed target words include Olympics (171), Summer (104), Games (89), Olympic (76)",
        .cluster = 100U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0764358863F, 0.104012996F, -0.0604425967F, 0.0870982260F,
            0.0558346994F, -0.0691482127F, -0.0623566546F, 0.0588547438F,
            -0.0593934134F, -0.0662274733F, 0.0878921673F, 0.0623992644F,
            0.0771734640F, -0.0623142011F, -0.0569122508F, -0.0738412589F,
            -0.0542466938F, -0.0644125491F, -0.0694459453F, -0.0795552656F,
            0.0856662020F, 0.0809587687F, 0.0713033378F, 0.0672340617F,
            0.0700273141F, 0.0858502761F, 0.0868288279F, -0.0670781657F,
            -0.0675885528F, 0.0765211061F, 0.0627961829F, 0.0715159476F
        }
    },
    {
        .id = "encyclopedia:cluster-030-0001:0006", .category = 33U, .observations = UINT64_C(4489),
        .description = "Video Games and Fantasy; article-title cues include Olympics, Church, Summer, Paralympics, Winter, Games; observed target words include Olympic (50)",
        .cluster = 101U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            0.0186004862F, 0.00455494970F, -0.0302175209F, 0.0247962903F,
            0.00985813327F, -0.0389467888F, -0.0169858839F, -0.0530579835F,
            0.0169727746F, 0.0117877200F, 0.00401675841F, -0.0299944114F,
            0.00367546431F, -0.0106719714F, -0.0115252156F, 0.00509314332F,
            -0.0321471393F, 0.00282223034F, 0.00140455400F, -0.0330791287F,
            -0.0102781933F, -0.0188499205F, -0.00133891101F, -0.0357832611F,
            0.0185348168F, 0.0146756079F, -0.0108032357F, -0.0232866816F,
            -0.0234704614F, 0.0274871979F, -0.000118141077F, 0.0342212021F
        }
    },
};

static const cgai_knowledge_module module = {
    .category = {
        .index = 33U,
        .key = "encyclopedia/cluster-030",
        .name = "Video Games and Fantasy: Olympics, Church, Summer",
        .description = "Video Games and Fantasy; article-title cues include Olympics, Church, Summer, Paralympics, Winter, Games",
        .count = sizeof(centroids) / sizeof(centroids[0])
    },
    .rows = centroids
};

const cgai_knowledge_module *cgai_knowledge_encyclopedia_cluster_030(void) { return &module; }
