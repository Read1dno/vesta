#include "test_support.hpp"
#include <core/math/ray_capsule.hpp>

int main() {
    using foundation::vec3;
    float d{};
    const auto hit = [&](vec3 o, vec3 r, vec3 a, vec3 b, float radius, float expected) {
        VESTA_CHECK(foundation::ray_capsule_entry(o,r,a,b,radius,d));
        VESTA_CHECK(std::abs(d-expected)<0.0001f);
    };
    hit({0,0,0},{1,0,0},{10,0,-1},{10,0,1},2,8);
    hit({0,0,0},{2,0,0},{10,0,-1},{10,0,1},2,4);
    hit({0,2,0},{1,0,0},{10,0,-1},{10,0,1},2,10);
    hit({10,0,0},{1,0,0},{10,0,-1},{10,0,1},2,0);
    hit({10,0,-10},{0,0,1},{10,0,-1},{10,0,1},2,7);
    hit({0,0,0},{1,0,0},{10,0,0},{10,0,0},2,8);
    hit({9,0,0},{-1,0,0},{10,0,0},{10,0,0},2,0);
    hit({0,0,0},{1,0,0},{10000,0,-1},{10000,0,1},2,9998);
    VESTA_CHECK(!foundation::ray_capsule_entry({0,3,0},{1,0,0},{10,0,-1},{10,0,1},2,d));
    VESTA_CHECK(!foundation::ray_capsule_entry({0,0,0},{-1,0,0},{10,0,-1},{10,0,1},2,d));
    VESTA_CHECK(!foundation::ray_capsule_entry({0,0,0},{0,0,0},{10,0,-1},{10,0,1},2,d));
    VESTA_CHECK(!foundation::ray_capsule_entry({0,0,0},{1,0,0},{10,0,-1},{10,0,1},-2,d));
    VESTA_CHECK(!foundation::ray_capsule_entry({NAN,0,0},{1,0,0},{10,0,-1},{10,0,1},2,d));
    std::cout << "ray_capsule: first entry, tangent, inside, parallel, sphere and invalid inputs PASS\n";
}
