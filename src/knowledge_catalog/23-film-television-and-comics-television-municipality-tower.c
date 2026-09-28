/* Curated static knowledge baseline; maintain these C sources directly. */
/* Category: Film, Television, and Comics: Television, Municipality, Tower; centroids: 3. */
#include "../knowledge_catalog.h"

static const cgai_static_knowledge_centroid centroids[] = {
    {
        .id = "encyclopedia:cluster-020-0000:0008", .category = 23U, .observations = UINT64_C(3872),
        .description = "Film, Television, and Comics; article-title cues include Television, Municipality, Tower, Series, Switzerland, Spongebob; observed target words include Series (60), Municipality (49), Television (36), City (33)",
        .cluster = 69U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0913102478F, 0.0962410793F, -0.0546491444F, 0.0980671048F,
            0.0592603385F, -0.0728046894F, -0.0671131611F, 0.0819357112F,
            -0.0525795110F, -0.0677371174F, 0.0833510458F, 0.0656976029F,
            0.0753614753F, -0.0556535535F, -0.0745700002F, -0.0901688933F,
            -0.0628366396F, -0.0742047876F, -0.0492466986F, -0.0707349554F,
            0.0726372227F, 0.0778573230F, 0.0825747997F, 0.0770203099F,
            0.0541014224F, 0.0695174783F, 0.0888297260F, -0.0583319925F,
            -0.0819661096F, 0.0792114511F, 0.0479226001F, 0.0753006190F
        }
    },
    {
        .id = "encyclopedia:cluster-020-0000:0015", .category = 23U, .observations = UINT64_C(3599),
        .description = "Film, Television, and Comics; article-title cues include Television, Municipality, Tower, Series, Switzerland, Spongebob; observed target words include References (83), Websites (77)",
        .cluster = 70U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0755275711F, 0.0798990503F, -0.0692568570F, 0.0693059564F,
            -0.0796535388F, 0.0741358921F, -0.0727931708F, 0.0583197586F,
            -0.0582215525F, -0.0787693635F, 0.0826005042F, -0.0768046677F,
            0.0694861114F, 0.0596787743F, 0.0559457429F, -0.0680942908F,
            0.0664569810F, 0.0727769062F, 0.0846961141F, 0.0687328801F,
            0.0685364380F, -0.0722038597F, 0.0790149942F, -0.0741030425F,
            0.0653928444F, -0.0659822524F, -0.0532605872F, 0.0635426715F,
            -0.0638210252F, 0.0700100437F, -0.0624784417F, -0.0671282709F
        }
    },
    {
        .id = "encyclopedia:cluster-020-0001:0015", .category = 23U, .observations = UINT64_C(3902),
        .description = "Film, Television, and Comics; article-title cues include Television, Municipality, Tower, Series, Switzerland, Spongebob; observed target words include Websites (82), Television (58)",
        .cluster = 71U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0356241502F, 0.0177290421F, 0.0316072106F, 0.0360771455F,
            -0.0546669886F, 0.0424196869F, -0.0332985185F, -0.0324377641F,
            -0.00279375468F, -0.0213533435F, 0.0310181789F, -0.0491851978F,
            0.0483697131F, 0.0452135615F, -0.0441261679F, 0.0437637605F,
            0.00839637220F, 0.0299762283F, 0.0126549536F, 0.0413475707F,
            -0.0506499633F, -0.0549237132F, 0.0127606671F, 0.0500006527F,
            -0.0438091047F, 0.0468595438F, 0.0244189408F, -0.0271975920F,
            0.0361979492F, 0.0307011083F, -0.00362433167F, -0.0541536175F
        }
    },
};

static const cgai_knowledge_module module = {
    .category = {
        .index = 23U,
        .key = "encyclopedia/cluster-020",
        .name = "Film, Television, and Comics: Television, Municipality, Tower",
        .description = "Film, Television, and Comics; article-title cues include Television, Municipality, Tower, Series, Switzerland, Spongebob",
        .count = sizeof(centroids) / sizeof(centroids[0])
    },
    .rows = centroids
};

const cgai_knowledge_module *cgai_knowledge_encyclopedia_cluster_020(void) { return &module; }
