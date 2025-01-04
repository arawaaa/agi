#pragma once

#include "job.h"
#include "machine.h"

#include <boost/json.hpp>

#include <vector>
#include <list>
#include <optional>
#include <memory>

#include "ortools/base/init_google.h"
#include "ortools/base/logging.h"
#include "ortools/base/path.h"
#include "ortools/packing/binpacking_2d_parser.h"
#include "ortools/packing/multiple_dimensions_bin_packing.pb.h"
#include "ortools/sat/cp_model.h"
#include "ortools/sat/cp_model.pb.h"
#include "ortools/sat/cp_model_solver.h"
#include "ortools/sat/sat_parameters.pb.h"
#include "ortools/sat/util.h"

struct TaskInfo {
    operations_research::sat::IntervalVar duration;
    operations_research::sat::IntVar start;
    operations_research::sat::IntVar end;
    operations_research::sat::IntVar machine_num;
};

using schedule = std::vector<std::pair<int, std::list<Job*>>>;


class Sorter
{
    std::vector<std::shared_ptr<Job>> jobs;
    std::vector<std::shared_ptr<Machine>> machines;

    std::chrono::year_month_day start_day;

    operations_research::Domain getWorkerAvailability();

    void enforceOverlapConstraints(operations_research::sat::CpModelBuilder& model, TaskInfo& t1, TaskInfo& t2);
public:
    /**
     * Default constructor
     */
    Sorter() = delete;
    Sorter(std::string info);

    std::vector<std::shared_ptr<Machine>> getCompatibleFor(std::shared_ptr<Job>& job);

    std::optional<schedule> matchToSchedule();

};
