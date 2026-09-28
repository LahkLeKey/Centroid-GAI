/* Curated static knowledge baseline; maintain these C sources directly. */
/* Category: Sports and Competitions: Footballer, Football, League; centroids: 3. */
#include "../knowledge_catalog.h"

static const cgai_static_knowledge_centroid centroids[] = {
    {
        .id = "encyclopedia:cluster-026-0001:0015", .category = 29U, .observations = UINT64_C(24616),
        .description = "Sports and Competitions; article-title cues include Footballer, Football, League, Team, Cup, Uefa; observed target words include Rowspan (767), Colspan (289)",
        .cluster = 87U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            0.145894840F, -0.128162235F, 0.151182011F, -0.141128138F,
            0.111459643F, -0.150767997F, 0.145789385F, -0.124846295F,
            -0.134194687F, -0.154914558F, -0.151230574F, -0.126254052F,
            -0.149459064F, 0.132174432F, -0.137137860F, -0.127111197F,
            0.136185467F, 0.127217814F, 0.143568009F, 0.131948069F,
            0.131419480F, 0.125172600F, 0.125337347F, 0.124221116F,
            -0.138301849F, -0.134438530F, -0.135407746F, 0.139265940F,
            -0.148237765F, -0.127406314F, 0.134989798F, -0.143505543F
        }
    },
    {
        .id = "encyclopedia:cluster-026-0002:0015", .category = 29U, .observations = UINT64_C(39441),
        .description = "Sports and Competitions; article-title cues include Footballer, Football, League, Team, Cup, Uefa; observed target words include Rowspan (807), Colspan (502)",
        .cluster = 88U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            0.150324121F, -0.139422014F, 0.156033382F, -0.149009660F,
            0.0928270966F, -0.123445131F, 0.118364796F, -0.136630699F,
            -0.106270477F, -0.158269197F, -0.156241730F, -0.107164942F,
            -0.152336746F, 0.142858163F, -0.110760845F, -0.102876157F,
            0.143961713F, 0.103394523F, 0.115179732F, 0.107033886F,
            0.107431531F, 0.102431580F, 0.100833528F, 0.132880151F,
            -0.110700712F, -0.147499800F, -0.114393950F, 0.115208842F,
            -0.151570246F, -0.135163248F, 0.106146969F, -0.148235321F
        }
    },
    {
        .id = "encyclopedia:cluster-026-0003:0015", .category = 29U, .observations = UINT64_C(21084),
        .description = "Sports and Competitions; article-title cues include Footballer, Football, League, Team, Cup, Uefa; observed target words include Rowspan (412), Colspan (242)",
        .cluster = 89U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            0.143842444F, -0.138158172F, 0.150480568F, -0.136177003F,
            0.0916917995F, -0.122884363F, 0.114751272F, -0.126331419F,
            -0.105064988F, -0.150877595F, -0.145628884F, -0.102124311F,
            -0.144471750F, 0.136431590F, -0.107937939F, -0.105146103F,
            0.139840990F, 0.107527129F, 0.113334559F, 0.105131723F,
            0.107719935F, 0.102521583F, 0.0996849313F, 0.130380526F,
            -0.105307870F, -0.136841759F, -0.110604167F, 0.115601547F,
            -0.145913318F, -0.124699056F, 0.105542757F, -0.142464831F
        }
    },
};

static const cgai_knowledge_module module = {
    .category = {
        .index = 29U,
        .key = "encyclopedia/cluster-026",
        .name = "Sports and Competitions: Footballer, Football, League",
        .description = "Sports and Competitions; article-title cues include Footballer, Football, League, Team, Cup, Uefa",
        .count = sizeof(centroids) / sizeof(centroids[0])
    },
    .rows = centroids
};

const cgai_knowledge_module *cgai_knowledge_encyclopedia_cluster_026(void) { return &module; }
