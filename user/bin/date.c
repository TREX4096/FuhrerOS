/* date — UTC wall clock (civil-from-days, no floating point) */
#include "fu.h"
int main(void)
{
	int64_t t = unix_time();
	int64_t days = t / 86400, rem = t % 86400;
	int64_t z = days + 719468, era = z / 146097, doe = z - era * 146097;
	int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	int64_t y = yoe + era * 400, doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
	int64_t d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9;
	y += m <= 2;
	printf("%04ld-%02ld-%02ld %02ld:%02ld:%02ld UTC\n", y, m, d, rem / 3600, (rem / 60) % 60, rem % 60);
	return 0;
}
