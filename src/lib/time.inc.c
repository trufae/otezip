#include <stdint.h>
#include <time.h>

/* Thread-safe localtime: localtime_s on Windows, localtime_r elsewhere */
static struct tm *otezip_localtime_r(const time_t *t, struct tm *out) {
#if defined(_WIN32) || defined(_WIN64)
	return localtime_s (out, t) == 0? out: NULL;
#else
	return localtime_r (t, out);
#endif
}

static inline int dim(int value, int min, int max) {
	return value < min? min: value > max? max
					: value;
}

static void otezip_get_dostime(uint16_t *dos_time, uint16_t *dos_date) {
	time_t now = time (NULL);
	struct tm tm_buf;
	struct tm *tm_ptr = NULL;
	if (now != (time_t)-1) {
		tm_ptr = otezip_localtime_r (&now, &tm_buf);
	}

	/* Default to DOS epoch start if anything fails */
	if (!tm_ptr) {
		 *dos_time = 0; /* 00:00:00 -> all zero */
		 *dos_date = 0; /* 1980-01-01 -> year offset 0, month 1, day 1 */
		/* encode date: year offset 0, month 1, day 1 */
		*dos_date = (uint16_t) (((0) << 9) | ((1) << 5) | 1);
		return;
	}

	/* Extract and clamp fields to valid ranges to avoid overflow or
	 * nonsensical dates on platforms with limited time_t ranges. */
	int year = tm_ptr->tm_year + 1900; /* full year */
	int year_off = dim (year - 1980, 0, 127); /* DOS stores 7 bits */
	int mon = dim (tm_ptr->tm_mon + 1, 1, 12);
	int day = dim (tm_ptr->tm_mday, 1, 31);
	int hour = dim (tm_ptr->tm_hour, 0, 23);
	int min = dim (tm_ptr->tm_min, 0, 59);
	int sec = dim (tm_ptr->tm_sec, 0, 59);
	/* DOS time stores seconds divided by 2 */
	int sec2 = dim (sec / 2, 0, 29);

	*dos_time = (uint16_t) ((hour << 11) | (min << 5) | sec2);
	*dos_date = (uint16_t) ((year_off << 9) | (mon << 5) | day);
}
