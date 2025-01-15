# Machine Scheduler
### Features: maintenance periods, restricted start times and multi-speed machines
Machine speed is specified in bags per hour, jobs are specified in # of bags. Jobs are matched to machines based on makespan minimizing objectives constrained to start in only certain shifts (see available in schema), only when the machine is not undergoing maintenance (see downtime) and only if the machine has the requisite features that the job requires (specified as key value pairs where values may either be yes/no features or range-based features).

To compile, you will need a C++20 compiler, Boost.Beast, Boost.Json, Boost.Container and Google's [Operational Research Tools](https://github.com/google/or-tools).

OR-Tools is used as a cmake subdirectory.
```
git clone <this_url>
cd sched_converter && mkdir build
git clone https://github.com/google/or-tools.git
cd build && cmake -DBUILD_DEPS=ON .. && cmake --build .
```
Built binaries will be in build/. -DBUILD_DEPS=ON can be omitted if requisite variable precision and COIN optimization tools are available.
