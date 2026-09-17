#pragma once

#include "cpufreq_common.h"
#include "probe_api.h"

#define DQS_MAGIC         0x44515330u   /* DQS0 */
#define DQS_COPY_TOO_BIG  0xBADC0DE1u
#define DQS_COPY_MISMATCH 0xBADC0DE2u

#define DQS_VALUES        16u
#define DQS_META          16u
#define DQS_RESULT_WORDS  (DQS_META + DQS_VALUES * 2u)
