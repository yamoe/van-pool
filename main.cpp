
#include <stdio.h>
#include <chrono>
#include "pool.h"


class ElapsedTimer {
	private:
		using Clock = std::chrono::high_resolution_clock;
		Clock::time_point start_;

	public:
		void start() noexcept
		{
			start_ = Clock::now();
		}

		double stop() noexcept
		{
			double t = static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start_).count());
			return t / 1000.0;
		}
};




int main()
{

	printf("\n\n---------------------------------------------------------------------------------------------\n");

	class Test {};
	uint64_t LOOP = 100000000;

	ElapsedTimer timer;

	timer.start();
	for (uint64_t i=0; i<LOOP; ++i) {
		Test* t = new Test;
		delete t;
	}
	printf("  %-20s : %lf msec\n", "new/delete", timer.stop());

	timer.start();
	for (uint64_t i=0; i<LOOP; ++i) {
		Test* t = van::pool::get_tls<Test>();
		van::pool::ret_tls(t);
	}
	printf("  %-20s : %lf msec\n", "tls class pool", timer.stop());

	timer.start();
	for (uint64_t i=0; i<LOOP; ++i) {
		Test* t1 = van::pool::get_singleton<Test>();
		van::pool::ret_singleton(t1);
	}
	printf("  %-20s : %lf msec\n", "singleton class pool", timer.stop());

	timer.start();
	for (uint64_t i=0; i<LOOP; ++i) {
		van::pool::Mem<1024>* t = van::pool::get_tls<1024>();
		van::pool::ret_tls(t);
	}
	printf("  %-20s : %lf msec\n", "tls mem pool", timer.stop());

	timer.start();
	for (uint64_t i=0; i<LOOP; ++i) {
		van::pool::Mem<1024>* t = van::pool::get_singleton<1024>();
		van::pool::ret_singleton(t);
	}
	printf("  %-20s : %lf msec\n", "singleton mem pool", timer.stop());


	printf("\n\n---------------------------------------------------------------------------------------------\n");
	van::pool::print_stat();
	

	printf("\n\nend\n\n");

	return 0;
}


