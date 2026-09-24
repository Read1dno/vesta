#include <render/overlay/present_policy.hpp>
#include <cstdio>

int main()
{
	render::present_recovery policy;
	auto now = render::present_recovery::clock::time_point{};
	if (policy.retry_surface(DXGI_ERROR_DEVICE_REMOVED, DXGI_ERROR_DEVICE_REMOVED, now))
		return 1;
	for (int i = 0; i < 3; ++i)
		if (!policy.retry_surface(DXGI_ERROR_INVALID_CALL, S_OK, now))
			return 2;
	if (policy.retry_surface(DXGI_ERROR_INVALID_CALL, S_OK, now))
		return 3;
	now += std::chrono::seconds(30);
	if (!policy.retry_surface(DXGI_ERROR_NOT_CURRENTLY_AVAILABLE, S_OK, now))
		return 4;
	if (policy.retry_surface(DXGI_ERROR_INVALID_CALL, DXGI_ERROR_DEVICE_RESET, now))
		return 5;
	if (policy.retry_surface(E_OUTOFMEMORY, S_OK, now))
		return 6;
	std::puts("PASS transient Present recovery is bounded; device failures are not retried blindly");
}
