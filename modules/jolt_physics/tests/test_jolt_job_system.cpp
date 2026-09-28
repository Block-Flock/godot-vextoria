#include "../spaces/jolt_job_system.h"

#include "core/os/semaphore.h"
#include "core/os/thread.h"
#include "tests/test_macros.h"

namespace TestJoltJobSystem {
#ifdef JPH_ENABLE_ASSERTS
static std::atomic_uint assertion_count{ 0 };

static bool count_assertion(const char *, const char *, const char *, uint32_t) {
	assertion_count.fetch_add(1);
	return false;
}

struct AssertionScope {
	JPH::AssertFailedFunction previous = JPH::AssertFailed;
	AssertionScope() {
		assertion_count.store(0);
		JPH::AssertFailed = &count_assertion;
	}
	~AssertionScope() { JPH::AssertFailed = previous; }
};
#endif

void check_released_jobs_at_shutdown() {
#ifdef JPH_ENABLE_ASSERTS
	AssertionScope assertions;
#endif
	JoltJobSystem *system = new JoltJobSystem();
	{
		// No worker or barrier: dropping the last reference queues the object
		// for reclamation, after the last possible post_step. The old adapter's
		// implicit destructor deterministically destroys a nonempty pool here.
		JPH::JobHandle job = static_cast<JPH::JobSystem *>(system)->CreateJob(
				"released shutdown job", JPH::Color::sWhite, [] {}, 1);
	}
	delete system;
#ifdef JPH_ENABLE_ASSERTS
	CHECK(assertion_count.load() == 0);
#endif
}

void check_independent_job_systems() {
#ifdef JPH_ENABLE_ASSERTS
	AssertionScope assertions;
#endif
	JoltJobSystem *a = new JoltJobSystem();
	JoltJobSystem *b = new JoltJobSystem();
	{
		JPH::JobHandle first = static_cast<JPH::JobSystem *>(a)->CreateJob("pool a", JPH::Color::sWhite, [] {}, 1);
		JPH::JobHandle second = static_cast<JPH::JobSystem *>(b)->CreateJob("pool b", JPH::Color::sWhite, [] {}, 1);
	}
	a->post_step();
	delete b;
	delete a;
#ifdef JPH_ENABLE_ASSERTS
	CHECK(assertion_count.load() == 0);
#endif
}

void check_running_worker_at_shutdown() {
#ifdef JPH_ENABLE_ASSERTS
	AssertionScope assertions;
#endif
	Semaphore entered;
	Semaphore release;
	std::atomic_uint completed{ 0 };
	JoltJobSystem *system = new JoltJobSystem();
	{
		JPH::JobHandle job = static_cast<JPH::JobSystem *>(system)->CreateJob(
				"running shutdown job", JPH::Color::sWhite, [&] {
					entered.post();
					release.wait();
					completed.fetch_add(1);
				});
		entered.wait();
	}
	Thread unblock;
	unblock.start([](void *p_release) { static_cast<Semaphore *>(p_release)->post(); }, &release);
	delete system;
	unblock.wait_to_finish();
	CHECK(completed.load() == 1);
#ifdef JPH_ENABLE_ASSERTS
	CHECK(assertion_count.load() == 0);
#endif
}
} // namespace TestJoltJobSystem
