#include <simulation/seed_receipt.hpp>
#include "test_support.hpp"

int main() {
    simulation::seed_receipt receipt;
    VESTA_CHECK(receipt.observe(1, 223.0f, 14278));
    receipt.arm(1, 223.0f, 14278);
    VESTA_CHECK(!receipt.consumed(1, 223.0f));
    VESTA_CHECK(receipt.observe(1, 223.09058f, 14278));
    VESTA_CHECK(receipt.consumed(1, 223.09058f));
    VESTA_CHECK(receipt.consumed(1, 223.09058f)); // A failed release may retry acknowledgement.
    receipt.acknowledge(223.09058f);
    VESTA_CHECK(!receipt.consumed(1, 223.09058f));
    VESTA_CHECK(receipt.observe(1, 223.0f, 14278));
    receipt.arm(1, 223.0f, 14278);
    VESTA_CHECK(!receipt.consumed(1, 223.09058f)); // Replayed confirmation is not a new shot.
    receipt.acknowledge(223.09058f);
    VESTA_CHECK(receipt.consumed(1, 223.2f));
    receipt.acknowledge(223.2f);
    VESTA_CHECK(!receipt.observe(1, 223.09058f, 14276));
    VESTA_CHECK(!receipt.observe(1, 223.09058f, 14277));
    VESTA_CHECK(receipt.observe(1, 223.09058f, 14278));
    receipt.arm(1, 223.09058f, 14278);
    VESTA_CHECK(!receipt.consumed(1, 223.09058f));
    VESTA_CHECK(!receipt.observe(1, NAN, 14279));
    VESTA_CHECK(!receipt.consumed(1, NAN));
    VESTA_CHECK(!receipt.observe(0, 0, 14279));
    VESTA_CHECK(!receipt.observe(1, -1, 14279));
    VESTA_CHECK(!receipt.observe(1, 224, 0));
    VESTA_CHECK(receipt.observe(2, 0, 14279));
    VESTA_CHECK(!receipt.consumed(1, 224));
    VESTA_CHECK(!receipt.consumed(2, 1));
    receipt.arm(2, 0, 14279);
    VESTA_CHECK(!receipt.consumed(2, 0));
    VESTA_CHECK(receipt.observe(2, .25f, 14279));
    VESTA_CHECK(receipt.consumed(2, .25f));
    receipt.acknowledge(.25f);
    receipt.reset();
    // A corrected time must not permanently prevent another input/confirmation.
    VESTA_CHECK(receipt.observe(1, 10.0f, 640));
    VESTA_CHECK(receipt.observe(1, 9.0f, 641));
    receipt.arm(1, 9.0f, 641);
    VESTA_CHECK(receipt.consumed(1, 9.25f));
    receipt.acknowledge(NAN);
    VESTA_CHECK(receipt.consumed(1, 9.25f));
    receipt.acknowledge(9.25f);
    VESTA_CHECK(!receipt.consumed(1, 9.25f));
    VESTA_CHECK(receipt.observe(1, 9.0f, 642));
    receipt.arm(1, 9.0f, 642);
    VESTA_CHECK(!receipt.consumed(1, 9.25f));
    VESTA_CHECK(receipt.consumed(1, 9.5f));
    receipt.acknowledge(9.5f);
    receipt.reset();
    VESTA_CHECK(receipt.observe(1, 0, 1));
    VESTA_CHECK(!receipt.consumed(1, 1));
    std::cout << "seed_receipt: fresh marker, replay, release retry, correction liveness, weapon switch and reset PASS\n";
}
