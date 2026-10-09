#include "nadine_p99_arm_roll.h"
#include <cstdio>
#include <limits>

namespace roll = coop::p99_arm_roll;
using roll::Vector;
using roll::Quaternion;
using roll::Locator;
int checks = 0, failures = 0;
void check(bool value, const char* label) {
    ++checks;
    if (!value) { ++failures; std::fprintf(stderr, "FAIL: %s\n", label); }
}
Quaternion multiply(const Quaternion& a, const Quaternion& b) {
    return {a[3]*b[0]+a[0]*b[3]+a[1]*b[2]-a[2]*b[1],
            a[3]*b[1]-a[0]*b[2]+a[1]*b[3]+a[2]*b[0],
            a[3]*b[2]+a[0]*b[1]-a[1]*b[0]+a[2]*b[3],
            a[3]*b[3]-a[0]*b[0]-a[1]*b[1]-a[2]*b[2]};
}
Quaternion inverse(Quaternion q) { q[0]=-q[0]; q[1]=-q[1]; q[2]=-q[2]; return q; }
Quaternion axis_angle(Vector axis, float degrees) {
    roll::normalize(axis);
    const float half=degrees*.008726646259971648F;
    return {axis[0]*std::sin(half),axis[1]*std::sin(half),axis[2]*std::sin(half),std::cos(half)};
}
Locator make(Vector p, Quaternion q = {0,0,0,1}) {
    return {p[0],p[1],p[2],0,q[0],q[1],q[2],q[3]};
}
float distance(Vector a, Vector b) {
    const auto d=roll::subtract(a,b); return std::sqrt(roll::dot(d,d));
}
Locator rotate_about(const Locator& frame, Vector pivot, Quaternion delta) {
    const auto p=roll::rotate(delta,roll::subtract(roll::position(frame),pivot));
    return make({pivot[0]+p[0],pivot[1]+p[1],pivot[2]+p[2]},
                multiply(delta,roll::quaternion(frame)));
}
Locator rigid_transform(const Locator& frame, Vector translation, Quaternion rotation) {
    const auto p=roll::rotate(rotation,roll::position(frame));
    return make({p[0]+translation[0],p[1]+translation[1],p[2]+translation[2]},
                multiply(rotation,roll::quaternion(frame)));
}
using Chain = std::array<Locator,5>;
Chain forward(const Locator& parent, const Chain& locals, float scale) {
    Chain world{};
    Locator preceding=parent;
    for(std::size_t i=0;i<locals.size();++i) {
        auto scaled=locals[i];
        for(std::size_t j=0;j<3;++j) scaled[j]*=scale;
        world[i]=coop::p99_support_constraint::compose_rigid(preceding,scaled);
        preceding=world[i];
    }
    return world;
}
// Model native14181F450: inverse(parentWS)*Dworld*parentWS, then PRE-multiply
// LOCAL Q. Rebuild parent WS between shoulder and wrist operations.
void native_delta(const Locator& parent, Chain& locals, float scale,
                  std::size_t ordinal, const Quaternion& delta) {
    const auto world=forward(parent,locals,scale);
    const auto parent_q=roll::quaternion(ordinal==0?parent:world[ordinal-1]);
    const auto relative=multiply(multiply(inverse(parent_q),delta),parent_q);
    const auto q=multiply(relative,roll::quaternion(locals[ordinal]));
    for(std::size_t j=0;j<4;++j) locals[ordinal][4+j]=q[j];
}
float qerror(const Locator& a, const Locator& b) {
    float direct=0,opposite=0;
    for(std::size_t j=4;j<8;++j) {
        direct=std::max(direct,std::abs(a[j]-b[j]));
        opposite=std::max(opposite,std::abs(a[j]+b[j]));
    }
    return std::min(direct,opposite);
}
int main() {
    // Pinned DOWN geometry in its right-prop frame; only tests use these world
    // fixture positions. Runtime has no copied shoulder/elbow/wrist positions.
    const auto s=make({.193097386F,.296404094F,-.310035442F},axis_angle({1,2,3},51));
    const auto e=make({.248754714F,.049665688F,-.177770723F},axis_angle({2,-1,3},-42));
    const auto w=make({.071908736F,-.008718301F,-.017715590F},axis_angle({-1,4,2},79));
    const auto rp=make({0,0,0});
    const auto pivot=roll::position(s);
    const auto axis=roll::subtract(roll::position(w),pivot);
    for (const float degrees : {-170.F,-90.F,-30.F,0.F,30.F,90.F,170.F}) {
        const auto distortion=axis_angle(axis,degrees);
        const auto wrong_s=rotate_about(s,pivot,distortion);
        const auto wrong_e=rotate_about(e,pivot,distortion);
        roll::Plan plan{};
        check(roll::plan(wrong_s,wrong_e,w,rp,&plan),"admit nondegenerate arm");
        check(distance(plan.expected_elbow,roll::position(e))<2e-6F,"recover authored elbow");
        const auto new_s=rotate_about(wrong_s,pivot,plan.world_delta);
        const auto new_e=rotate_about(wrong_e,pivot,plan.world_delta);
        auto new_w=rotate_about(w,pivot,plan.world_delta);
        const auto restored_q=multiply(inverse(plan.world_delta),roll::quaternion(new_w));
        check(distance(roll::position(new_s),pivot)<2e-6F,"shoulder anchor unchanged");
        check(distance(roll::position(new_w),roll::position(w))<2e-6F,"wrist endpoint unchanged");
        float qerr=0;
        for(std::size_t i=0;i<4;++i) qerr=std::max(qerr,std::abs(restored_q[i]-w[4+i]));
        check(qerr<2e-6F,"wrist counter rotation restores world orientation");
        check(std::abs(distance(roll::position(new_s),roll::position(new_e))-
                       distance(pivot,roll::position(e)))<2e-6F,"upper length unchanged");
        check(std::abs(distance(roll::position(new_e),roll::position(new_w))-
                       distance(roll::position(e),roll::position(w)))<2e-6F,"lower length unchanged");
        auto normal=roll::cross(roll::subtract(roll::position(new_e),pivot),
                                roll::subtract(roll::position(new_w),roll::position(new_e)));
        check(roll::normalize(normal),"result bend is nondegenerate");
        check(roll::dot(normal,plan.desired_normal)>.99999F,"desired bend plane reached");
        const auto rotation=axis_angle({2,-3,1},63);
        const Vector translation{5,-7,12};
        roll::Plan world{};
        check(roll::plan(rigid_transform(wrong_s,translation,rotation),
                         rigid_transform(wrong_e,translation,rotation),
                         rigid_transform(w,translation,rotation),
                         rigid_transform(rp,translation,rotation),&world),"admit changed world frame");
        check(distance(world.expected_elbow,
              roll::position(rigid_transform(e,translation,rotation)))<1e-5F,"world frame covariance");
    }
    roll::Plan result{};
    check(!roll::plan(s,e,s,rp,&result),"zero wrist axis refused");
    check(!roll::plan(make({0,0,0}),make({1,0,0}),make({2,0,0}),rp,&result),"collinear arm refused");
    auto invalid=w; invalid[4]=std::numeric_limits<float>::quiet_NaN();
    check(!roll::plan(s,e,invalid,rp,&result),"NaN refused");
    invalid=rp; invalid[7]=2;
    check(!roll::plan(s,e,w,invalid,&result),"nonunit locator refused");
    const auto parallel=roll::kAuthoredDownNormal;
    check(!roll::plan(make({0,0,0}),make({1,0,0}),make(parallel),rp,&result),"parallel authored normal refused");
    check(!roll::plan(s,e,w,rp,nullptr),"null output refused");
    check(result.world_delta==Quaternion{},"refusal clears output");
    for(const float scale:{.75F,1.F,1.4F}) {
        const auto parent=make({-2,3,1},axis_angle({2,-1,4},58));
        const auto distortion=axis_angle(axis,83);
        Chain desired{rotate_about(s,pivot,distortion),rotate_about(e,pivot,distortion),w,
            coop::p99_support_constraint::compose_rigid(w,make({.02F,.03F,-.01F},axis_angle({1,2,-3},45))),
            {}};
        desired[4]=coop::p99_support_constraint::compose_rigid(desired[3],
            make({-.01F,.04F,.03F},axis_angle({-3,1,2},-37)));
        Chain locals{};
        for(std::size_t i=0;i<locals.size();++i) {
            locals[i]=coop::p99_support_constraint::make_relative_rigid(i==0?parent:desired[i-1],desired[i]);
            for(std::size_t j=0;j<3;++j) locals[i][j]/=scale;
        }
        const auto before=forward(parent,locals,scale);
        roll::Plan correction{};
        check(roll::plan(before[0],before[1],before[2],rp,&correction),"native chain admits roll");
        native_delta(parent,locals,scale,0,correction.world_delta);
        const auto intermediate=forward(parent,locals,scale);
        check(qerror(intermediate[2],before[2])>.1F,"shoulder-only would rotate wrist");
        native_delta(parent,locals,scale,2,inverse(correction.world_delta));
        const auto after=forward(parent,locals,scale);
        check(distance(roll::position(after[1]),correction.expected_elbow)<3e-6F,"native local helpers reach elbow");
        for(std::size_t i:{0u,2u,3u,4u}) {
            check(distance(roll::position(after[i]),roll::position(before[i]))<3e-6F,"native chain endpoint invariant");
        }
        for(std::size_t i:{2u,3u,4u}) check(qerror(after[i],before[i])<3e-6F,"native distal orientation invariant");
    }
    auto near_unit=rigid_transform(rp,{0,0,0},axis_angle({1,2,3},31));
    auto exact=near_unit;
    for(std::size_t j=4;j<8;++j) near_unit[j]*=std::sqrt(.9995F);
    roll::Plan exact_plan{},near_plan{};
    check(roll::plan(s,e,w,exact,&exact_plan)&&roll::plan(s,e,w,near_unit,&near_plan),"near unit prop admitted");
    check(distance(exact_plan.expected_elbow,near_plan.expected_elbow)<2e-6F,"prop normalization preserves requested plane");
    // PID21244 exposed a missing sufficiency condition: a numerically nonzero
    // bend normal may exist on an almost fully extended arm. Plane alignment
    // can then pass without visibly moving the elbow. This is a mathematical
    // counterexample, not copied low-precision world-coordinate telemetry.
    const auto almost_s=make({0,0,0});
    const auto almost_e=make({.2854322F,.0001F,0});
    const auto almost_w=make({.5309945F,0,0});
    roll::Plan almost{};
    check(roll::plan(almost_s,almost_e,almost_w,rp,&almost),"nearly straight arm has a numerical plane");
    check(std::abs(almost.angle_degrees)>20.F,"large roll angle does not imply elbow displacement");
    check(distance(almost.expected_elbow,roll::position(almost_e))<=.000201F,
          "endpoint-preserving roll cannot restore bent arm");
    std::printf("nadine_p99_arm_roll_tests: %d checks, %d failures\n",checks,failures);
    return failures==0?0:1;
}
