#include <core/memory/read_pair.hpp>
#include <cstdio>
#include <random>
int main()
{
	std::mt19937_64 random(5209);
	for (int trial = 0; trial < 10000; ++trial)
	{
		std::array<std::byte, 20> bytes{};
		const std::uintptr_t pointer = random();
		const std::uint32_t serial = static_cast<std::uint32_t>(random());
		std::memcpy(bytes.data(), &pointer, 8);
		std::memcpy(bytes.data() + 16, &serial, 4);
		for (int mode = 0; mode < 5; ++mode)
		{
			unsigned calls = 0;
			auto read = [&](std::uintptr_t address, void *out, std::size_t n) {
				++calls;
				if (address < 0x10000 || address + n > 0x10000 + bytes.size())
					return false;
				std::memcpy(out, bytes.data() + address - 0x10000, n);
				if (mode && n == 20)
					return false;
				if ((mode == 2 || mode == 4) && address == 0x10000)
					return false;
				if ((mode == 3 || mode == 4) && address == 0x10010)
					return false;
				return true;
			};
			const auto result = foundation::read_pair<std::uintptr_t, std::uint32_t, 16>(0x10000, read);
			if (result.first != ((mode == 2 || mode == 4) ? 0 : pointer) ||
			    result.second != ((mode == 3 || mode == 4) ? 0 : serial))
				return 1;
			if (calls != (mode ? 3u : 1u))
				return 2;
		}
	}
	std::puts("PASS read_pair: 50000 complete/partial/gap/scalar-failure cases; "
	          "successful pair uses one read");
}
