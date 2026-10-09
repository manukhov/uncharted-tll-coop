#include "nadine_p99_arm_reach.h"
#include <cstdio>
#include <limits>

namespace reach=coop::p99_arm_reach;
int checks=0, failures=0;
void check(bool value,const char* message) {
    ++checks;
    if(!value) { ++failures; std::fprintf(stderr,"FAIL: %s\n",message); }
}
bool near(double a,double b,double tolerance=1e-6) { return std::abs(a-b)<=tolerance; }
int main() {
    const reach::Chain before{{{0,0,0},{.3F,.4F,0},{.6F,0,0}}};
    const reach::Chain straight{{{0,0,0},{.5F,0,0},{1,0,0}}};
    const reach::Chain bent{{{0,0,0},{.4F,.3F,0},{.8F,0,0}}};
    reach::Metrics m{};
    check(reach::analyze(before,straight,{1.5F,0,0},&m),"admit saturated target");
    check(near(m.upper_length,.5)&&near(m.lower_length,.5),"measure lengths");
    check(near(m.target_excess,.5)&&near(m.target_error,.5),"target really exceeds reach");
    check(near(m.clamped_target_error,0),"solved wrist on clamped target ray");
    check(near(m.pre_elbow_radius,.4)&&near(m.post_elbow_radius,0),"measure loss of bend");
    check(near(m.pre_reach,.6)&&near(m.post_reach,1),"observe reach increase");
    check(near(m.segment_length_error,0)&&near(m.shoulder_shift,0),"geometry preserved");
    check(reach::analyze(before,bent,{.8F,0,0},&m),"admit reachable bent target");
    check(m.target_excess<-.19&&near(m.target_error,0),"reachable target succeeds");
    check(near(m.post_elbow_radius,.3)&&near(m.clamped_target_error,0),"reachable output keeps bend");
    check(reach::analyze(before,before,{.8F,0,0},&m),"admit solver mismatch observation");
    check(m.target_excess<0&&near(m.target_error,.2)&&near(m.clamped_target_error,.2),
          "reachable target mismatch differs from saturation");
    check(reach::analyze(before,straight,{.8F,0,0},&m),"straight output with reachable input");
    check(m.target_excess<0&&near(m.post_elbow_radius,0)&&m.clamped_target_error>.19,
          "straight output alone does not prove unreachable input");
    auto translated_before=before,translated_straight=straight;
    const reach::Point shift{-760,5,383};
    for(auto* chain:{&translated_before,&translated_straight})
        for(auto& point:*chain) for(std::size_t i=0;i<3;++i) point[i]+=shift[i];
    check(reach::analyze(translated_before,translated_straight,{-758.5F,5,383},&m),
          "world offset admitted");
    check(near(m.target_excess,.5,1e-4)&&near(m.clamped_target_error,0,1e-4),
          "subtract coordinates in double without inventing input precision");
    // PID17512, first read-only 1418F1C43 pair: a reachable target can still
    // remove most incoming bend. These are exact logged float coordinates,
    // not geometry borrowed from the earlier rejected writer's game state.
    const reach::Chain observed_before{{
        {-759.64215087890625F,5.1784367561340332F,383.00552368164062F},
        {-759.630859375F,4.9667291641235352F,383.19662475585938F},
        {-759.5126953125F,4.7632112503051758F,383.26678466796875F}}};
    const reach::Chain observed_after{{observed_before[0],
        {-759.5791015625F,4.9485926628112793F,383.16256713867188F},
        {-759.5076904296875F,4.7469754219055176F,383.283203125F}}};
    check(reach::analyze(observed_before,observed_after,
          {-759.5076904296875F,4.7469663619995117F,383.283203125F},&m),
          "admit observed near-limit reachable request");
    check(near(m.target_excess,-.000568370142218444,1e-10)&&m.target_excess<0,
          "observed request is inside reach, not saturated");
    check(m.target_error<1e-5&&near(m.shoulder_shift,0),
          "native output reaches observed target without shoulder translation");
    check(near(m.pre_elbow_radius,.07806385547406793,1e-10)&&
          near(m.post_elbow_radius,.012248640232800175,1e-10),
          "native reachable solve still removes most elbow radius");
    // Offline RP*C alternatives in that SAME state, not native observations
    // with either donor installed. Raw double compositions rounded to float.
    check(reach::analyze(observed_before,observed_after,
          {-759.5513951441262F,4.687599462742875F,383.2425686088694F},&m),
          "admit modeled DOWN target in observed right-prop frame");
    check(near(m.target_excess,.02158837090191068,1e-4),
          "DOWN-only substitution would exceed reach in this state");
    check(reach::analyze(observed_before,observed_after,
          {-759.562951085552F,4.678008820376497F,383.2899068276225F},&m),
          "admit modeled UP target in observed right-prop frame");
    check(near(m.target_excess,.05001682786283457,1e-4),
          "UP-only substitution would also exceed reach in this state");
    auto changed=bent; changed[0][1]+=.1F;
    check(reach::analyze(before,changed,{.8F,0,0},&m)&&m.shoulder_shift>.09&&
          m.segment_length_error>.04,"shoulder and segment drift remains visible");
    auto invalid=before; invalid[1][0]=std::numeric_limits<float>::quiet_NaN();
    check(!reach::analyze(invalid,bent,{.8F,0,0},&m),"NaN refused");
    check(near(m.upper_length,0),"refusal clears result");
    check(!reach::analyze(before,bent,{0,0,0},&m),"zero target axis refused");
    invalid=before; invalid[1]=invalid[0];
    check(!reach::analyze(invalid,bent,{.8F,0,0},&m),"zero segment refused");
    check(!reach::analyze(before,bent,{.8F,0,0},nullptr),"null output refused");
    std::printf("nadine_p99_arm_reach_tests: %d checks, %d failures\n",checks,failures);
    return failures==0?0:1;
}
