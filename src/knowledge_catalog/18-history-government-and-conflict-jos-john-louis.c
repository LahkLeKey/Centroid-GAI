/* Curated static knowledge baseline; maintain these C sources directly. */
/* Category: History, Government, and Conflict: Jos, John, Louis; centroids: 3. */
#include "../knowledge_catalog.h"

static const cgai_static_knowledge_centroid centroids[] = {
    {
        .id = "encyclopedia:cluster-015-0000:0007", .category = 18U, .observations = UINT64_C(5466),
        .description = "History, Government, and Conflict; article-title cues include Jos, John, Louis, British, Class, Politician",
        .cluster = 54U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            0.0124189975F, 0.00327723497F, -0.0301958025F, 0.00260885525F,
            -0.0355105475F, -0.0144133829F, 0.0245900713F, 0.00240402599F,
            0.0319638364F, 0.00302927662F, -0.0367180072F, 0.00758938305F,
            -0.0318883359F, 0.0235551000F, 0.0253985729F, 0.0133353267F,
            -0.0203425661F, -0.0423884876F, 0.0243528858F, -0.0284925736F,
            -0.0152650140F, -0.00655448716F, -0.0211726874F, 0.0238246396F,
            -0.0241911542F, 0.0250104722F, 0.00199436722F, 0.0514655970F,
            0.0366209336F, -0.0120632527F, 0.0234796908F, -0.0387123637F
        }
    },
    {
        .id = "encyclopedia:cluster-015-0001:0011", .category = 18U, .observations = UINT64_C(4447),
        .description = "History, Government, and Conflict; article-title cues include Jos, John, Louis, British, Class, Politician; observed target words include American (102)",
        .cluster = 55U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            0.0711162314F, 0.0672866628F, 0.0782581866F, 0.0857316926F,
            -0.0647691265F, -0.0763237029F, -0.0709703863F, -0.0747469142F,
            -0.0786292553F, 0.0867253318F, 0.0759260505F, -0.0716594383F,
            0.0787220970F, -0.0806168541F, 0.0793315619F, -0.0718847290F,
            0.0705330744F, 0.0648486167F, -0.0821274221F, 0.0621189475F,
            0.0746010765F, 0.0724809542F, 0.0683599487F, -0.0684926212F,
            0.0551889092F, 0.0682805181F, 0.0703608766F, 0.0852015391F,
            -0.0610588044F, -0.0762573108F, 0.0774367452F, -0.0729181841F
        }
    },
    {
        .id = "encyclopedia:cluster-015-0002:0014", .category = 18U, .observations = UINT64_C(4250),
        .description = "History, Government, and Conflict; article-title cues include Jos, John, Louis, British, Class, Politician; observed target words include References (226), Died (166)",
        .cluster = 56U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0768110976F, 0.0876674131F, -0.0609775148F, 0.0559723489F,
            -0.0548631959F, 0.0750643983F, -0.0928806588F, 0.0281733777F,
            -0.0732617900F, -0.0809845254F, 0.0958477259F, -0.0915494934F,
            0.0562495776F, 0.0387522019F, 0.0449775420F, -0.0817885846F,
            0.0761734694F, 0.0758406594F, 0.0806241482F, 0.0533796512F,
            0.0631405488F, -0.0772271678F, 0.0597436242F, -0.0778787956F,
            0.0418162942F, -0.0294489060F, -0.0302115083F, 0.0531577729F,
            -0.0737471730F, 0.0697815865F, -0.0372132696F, -0.0757021233F
        }
    },
};

static const cgai_knowledge_module module = {
    .category = {
        .index = 18U,
        .key = "encyclopedia/cluster-015",
        .name = "History, Government, and Conflict: Jos, John, Louis",
        .description = "History, Government, and Conflict; article-title cues include Jos, John, Louis, British, Class, Politician",
        .count = sizeof(centroids) / sizeof(centroids[0])
    },
    .rows = centroids
};

const cgai_knowledge_module *cgai_knowledge_encyclopedia_cluster_015(void) { return &module; }
