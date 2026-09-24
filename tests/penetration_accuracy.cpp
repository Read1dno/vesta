#include "test_support.hpp"
#include <numbers>
#include <simulation/collision_segments.hpp>
#include <simulation/penetration_solver.hpp>
#include <core/math/ray_capsule.hpp>

using W=game::collision_world;
W::hit_entry hit(float d,bool enter,std::uint64_t id=1,float p=1,int type='C',float density=1000,std::uint32_t flags=0) {
    return {.distance=d,.surface={.penetration=p,.surface_type=static_cast<std::uint16_t>(type),.density=density,.interacts_as=flags},.is_enter=enter,.solid_id=id};
}
auto solve(std::vector<W::hit_entry> hits,float target=500,float damage=100,float rm=.5f,float pen=2,bool allow=true) {
    return simulation::detail::pass_through_world(game::collision_detail::build_segments(std::move(hits),target),target,pen,damage,rm,allow);
}
bool near(float a,float b){return std::abs(a-b)<.0001f;}
int main() {
    VESTA_CHECK(game::collision_detail::bullet_interaction_layer("passbullets")==0x2000);
    VESTA_CHECK(game::collision_detail::bullet_interaction_layer("PASSBULLETS")==0x2000);
    VESTA_CHECK(game::collision_detail::bullet_interaction_layer("grate")==0);
    static_assert(sizeof(W::triangle)==64);
    VESTA_CHECK(near(solve({})->damage,50));
    VESTA_CHECK(near(solve({},500,100,1)->damage,100));
    VESTA_CHECK(solve({},500,100,1,0,false));
    VESTA_CHECK(!solve({hit(10,true),hit(12,false)},100,100,1,0));
    VESTA_CHECK(!solve({hit(10,true),hit(12,false)},100,100,1,2,false));
    const auto walls=std::vector{hit(10,true),hit(20,false),hit(110,true,2),hit(120,false,2)};
    const auto records=game::collision_detail::build_segments(walls,1000);
    VESTA_CHECK(records.records.size()==5);
    VESTA_CHECK(records.records[0].start_distance==0 && records.records[0].end_distance==10);
    VESTA_CHECK(records.records[2].start_distance==20 && records.records[2].end_distance==110);
    VESTA_CHECK(records.records[4].start_distance==120 && records.records[4].end_distance==1000);
    auto expected=100.0f;
    for(float gap:{10.0f,90.0f}) {expected*=std::pow(.5f,gap/500);expected-=100.0f/24+5.625f+.16f*expected;}
    expected*=std::pow(.5f,880.0f/500);
    VESTA_CHECK(near(solve(walls,1000)->damage,expected));
    VESTA_CHECK(near(solve({hit(10,true),hit(12,false)},100,100,1)->damage,
        solve({hit(10,true,1,1,'C',4000),hit(12,false,1,1,'C',4000)},100,100,1)->damage));
    VESTA_CHECK(!solve({hit(3500,true),hit(3502,false)},3600,500,1));
    VESTA_CHECK(solve({hit(2999,true),hit(3000,false)},3100,500,1));
    VESTA_CHECK(!solve({hit(2999,true),hit(3000.01f,false)},3100,500,1));
    VESTA_CHECK(!solve({hit(10,true,1,.099f),hit(12,false)},100,500,1));
    std::vector<W::hit_entry> layers;
    for(int i=0;i<4;++i){layers.push_back(hit(10.0f+i*4,true,i,3));layers.push_back(hit(11.0f+i*4,false,i,3));}
    VESTA_CHECK(solve(layers,100,500,1)->penetrations==4);
    layers.push_back(hit(30,true,4,3));layers.push_back(hit(31,false,4,3));
    VESTA_CHECK(!solve(layers,100,500,1));
    auto duplicate=solve({hit(10,true),hit(10,true),hit(20,false)},100,100,1);
    VESTA_CHECK(duplicate && near(duplicate->damage,solve({hit(10,true),hit(20,false)},100,100,1)->damage));
    VESTA_CHECK(solve({hit(10,false)},100,100,1));
    VESTA_CHECK(!solve({hit(10,true)},100,100,1));
    for(int material:{'W','U','L','G','Y','C'}) {
        float factor=material=='L'?2.0f:material=='C'?1.0f:3.0f;
        float fraction=(material=='G'||material=='Y')?.05f:.16f;
        const auto loss=4.0f/factor/24+5.625f/factor+fraction*100;
        VESTA_CHECK(near(solve({hit(10,true,1,1,material),hit(12,false,1,1,material)},100,100,1)->damage,100-loss));
    }
    auto special=solve({hit(10,true,1,1,'C',1000,0x2000),hit(12,false,1,1,'C',1000,0x2000)},100,100,1);
    VESTA_CHECK(near(special->damage,100-(4.0f/32/24+5.625f/32+.00001f*100)));
    VESTA_CHECK(!solve({},NAN));VESTA_CHECK(!solve({hit(NAN,true)}));
    VESTA_CHECK(!solve({},100,100,NAN));
    float entry{};VESTA_CHECK(foundation::ray_capsule_entry({0,0,0},{1,0,0},{10,0,-1},{10,0,1},2,entry));
    VESTA_CHECK(entry==8 && solve({hit(9,true),hit(12,false)},entry,100,1));
    // Independent interval-union oracle with duplicate triangle contacts.
    std::uint32_t seed=721;
    const auto random=[&] {seed=1664525u*seed+1013904223u;return seed;};
    for(int iteration=0; iteration<1000; ++iteration) {
        std::vector<std::pair<float,float>> intervals;
        std::vector<W::hit_entry> contacts;
        for(int i=0;i<8;++i) {
            const auto a=float(random()%8000)/10;
            const auto b=a+float(random()%200+1)/10;
            intervals.emplace_back(a,b);
            contacts.push_back(hit(a,true,i));contacts.push_back(hit(a,true,i));
            contacts.push_back(hit(b,false,i));
        }
        std::ranges::sort(intervals);
        std::vector<std::pair<float,float>> expected_intervals;
        for(const auto& interval:intervals) {
            if(!expected_intervals.empty() && interval.first-expected_intervals.back().second<=1.0f/512)
                expected_intervals.back().second=std::max(expected_intervals.back().second,interval.second);
            else expected_intervals.push_back(interval);
        }
        auto built=game::collision_detail::build_segments(std::move(contacts),1000);
        VESTA_CHECK(!built.unresolved_before(1000));
        VESTA_CHECK(built.segments.size()==expected_intervals.size());
        for(std::size_t i=0;i<expected_intervals.size();++i) {
            VESTA_CHECK(near(built.segments[i].enter_distance,expected_intervals[i].first));
            VESTA_CHECK(near(built.segments[i].exit_distance,expected_intervals[i].second));
        }
        float cursor=0;
        for(const auto& r:built.records) {VESTA_CHECK(r.start_distance==cursor);cursor=r.end_distance;}
        VESTA_CHECK(cursor==1000);
    }
    std::cout << "penetration_accuracy: range/records/density/budget/materials/flags/first-hit PASS\n";
}
