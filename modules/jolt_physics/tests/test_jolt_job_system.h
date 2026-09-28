#pragma once

#include "tests/test_macros.h"

namespace TestJoltJobSystem {
void check_released_jobs_at_shutdown();
void check_independent_job_systems();
void check_running_worker_at_shutdown();

TEST_CASE("[JoltPhysics] Released jobs are reclaimed at shutdown") {
	check_released_jobs_at_shutdown();
}

TEST_CASE("[JoltPhysics] Completed queues belong to their job system") {
	check_independent_job_systems();
}

TEST_CASE("[JoltPhysics] Shutdown waits for running worker releases") {
	check_running_worker_at_shutdown();
}
} // namespace TestJoltJobSystem
