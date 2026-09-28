/* Curated static knowledge baseline; maintain these C sources directly. */
/* Category: Sports and Competitions: Hockey, Ice, John; centroids: 3. */
#include "../knowledge_catalog.h"

static const cgai_static_knowledge_centroid centroids[] = {
    {
        .id = "encyclopedia:cluster-016-0000:0008", .category = 19U, .observations = UINT64_C(4779),
        .description = "Sports and Competitions; article-title cues include Hockey, Ice, John, David, Jim, Bob; observed target words include Hockey (65), Born (57), Players (45)",
        .cluster = 57U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            0.0133905048F, 0.0528592058F, -0.0412688702F, 0.0823034048F,
            0.00813787058F, -0.0552388541F, 0.0362505093F, -0.0565211773F,
            0.0129712718F, 0.0210721437F, -0.0762739703F, 0.0758918375F,
            -0.0430321321F, 0.000974068360F, 0.0631423816F, -0.0149317943F,
            -0.00102338241F, -0.0365463793F, 0.0799852535F, -0.0291359555F,
            -0.0268427059F, 0.0430814549F, -0.0390124284F, 0.0202707089F,
            -0.0639316216F, 0.0726983994F, 0.0367807932F, 0.0861010253F,
            -0.0187294018F, -0.0536729358F, 0.0265837684F, -0.0813662931F
        }
    },
    {
        .id = "encyclopedia:cluster-016-0000:0015", .category = 19U, .observations = UINT64_C(5187),
        .description = "Sports and Competitions; article-title cues include Hockey, Ice, John, David, Jim, Bob; observed target words include Played (85), References (70), Websites (59)",
        .cluster = 58U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            -0.0750910342F, 0.0892119482F, -0.0286164582F, 0.0741937831F,
            -0.0774087608F, 0.0687065348F, -0.0797262341F, 0.0231976267F,
            -0.0745232105F, -0.0629130527F, 0.0685704276F, -0.0795784071F,
            0.0686726198F, 0.0417374335F, 0.0231862515F, -0.0572328977F,
            0.0659348965F, 0.0647986904F, 0.0716945156F, 0.0589823984F,
            0.0661280155F, -0.0784538761F, 0.0658212826F, -0.0594028160F,
            0.0558356121F, -0.0301273931F, -0.0226977523F, 0.0436686426F,
            -0.0561650358F, 0.0550404079F, -0.0605046675F, -0.0758295730F
        }
    },
    {
        .id = "encyclopedia:cluster-016-0002:0013", .category = 19U, .observations = UINT64_C(3862),
        .description = "Sports and Competitions; article-title cues include Hockey, Ice, John, David, Jim, Bob; observed target words include American (81)",
        .cluster = 59U,
        .vector = (const float[CGAI_STATIC_KNOWLEDGE_DIMENSIONS]){
            0.0710707605F, 0.0788522512F, 0.0830785334F, 0.0815834552F,
            -0.0740460306F, -0.0824531242F, -0.0732069835F, -0.0806984603F,
            -0.0753124505F, 0.0815375000F, 0.0826972649F, -0.0782419965F,
            0.0786691085F, -0.0860537440F, 0.0821020976F, -0.0859319642F,
            0.0724592581F, 0.0727032945F, -0.0795236230F, 0.0674699098F,
            0.0740001127F, 0.0774484724F, 0.0806682110F, -0.0800881013F,
            0.0739086941F, 0.0777384862F, 0.0790658891F, 0.0849857628F,
            -0.0800575465F, -0.0832921714F, 0.0817817450F, -0.0772348791F
        }
    },
};

static const cgai_knowledge_module module = {
    .category = {
        .index = 19U,
        .key = "encyclopedia/cluster-016",
        .name = "Sports and Competitions: Hockey, Ice, John",
        .description = "Sports and Competitions; article-title cues include Hockey, Ice, John, David, Jim, Bob",
        .count = sizeof(centroids) / sizeof(centroids[0])
    },
    .rows = centroids
};

const cgai_knowledge_module *cgai_knowledge_encyclopedia_cluster_016(void) { return &module; }
