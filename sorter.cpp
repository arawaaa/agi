#include "sorter.h"

#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "google/protobuf/text_format.h"
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

#include <iostream>
#include <map>
#include <vector>
#include <utility>

using namespace operations_research::sat;
using namespace operations_research;
using namespace std;

operations_research::Domain Sorter::getWorkerAvailability()
{
    std::vector<int64_t> intervals;
    for (int i = 0; i < 24 * 60 * 10; i += 24 * 60) { // 10 days
        intervals.push_back(i);
        intervals.push_back(i + 12 * 60);
    }
    return Domain::FromFlatSpanOfIntervals(intervals);
}

void Sorter::enforceOverlapConstraints(operations_research::sat::CpModelBuilder& model, TaskInfo& t1, TaskInfo& t2)
{
    BoolVar eq = model.NewBoolVar();

    BoolVar ahead1 = model.NewBoolVar();
    model.AddGreaterOrEqual(t1.start, t2.end).OnlyEnforceIf(ahead1);
    model.AddLessThan(t1.start, t2.end).OnlyEnforceIf(~ahead1);

    BoolVar behind1 = model.NewBoolVar();
    model.AddLessOrEqual(t1.end, t2.start).OnlyEnforceIf(behind1);
    model.AddGreaterThan(t1.end, t2.start).OnlyEnforceIf(~behind1);

    BoolVar combined_condition = model.NewBoolVar();
    model.AddBoolOr({ahead1, behind1}).OnlyEnforceIf(combined_condition);
    model.AddBoolAnd({ahead1.Not(), behind1.Not()}).OnlyEnforceIf(~combined_condition); // a /\ b == ~a \/ ~b

    model.AddEquality(t1.machine_num, t2.machine_num).OnlyEnforceIf(eq);
    model.AddNotEqual(t1.machine_num, t2.machine_num).OnlyEnforceIf(~eq);
    model.AddImplication(eq, combined_condition);
}

Sorter::Sorter(string info)
{
    auto parsed = boost::json::parse(info);

    using namespace literals;

    if (!parsed.is_object())
        throw runtime_error("Not an object");

    auto obj = parsed.as_object();

    if (!(obj.contains("machines") && obj.contains("jobs") && obj.contains("ymd")))
        throw runtime_error("Does not contain desired fields");

    auto machine_arr = obj["machines"];
    auto jobs_arr = obj["jobs"];
    auto ymd = obj["ymd"];

    if (!(machine_arr.is_array() && jobs_arr.is_array() && ymd.is_array() && ymd.as_array().size() == 3)) // ymd : year month day
        throw runtime_error("Wrong format");
    if (!(ymd.as_array()[0].is_int64() && ymd.as_array()[1].is_int64() && ymd.as_array()[2].is_int64()))
        throw runtime_error("Wrong format");

    start_day = year(ymd.as_array()[0].as_int64()) / month(ymd.as_array()[1].as_int64()) / day(ymd.as_array()[2].as_int64());

    int64_t i = 0;
    for (auto& val : machine_arr.as_array()) {
        i = val.as_object()["id"].as_int64();
        machines.emplace_back(make_shared<Machine>(val.as_object()["speed"].as_int64(), i, val.as_object()["features"].as_object()));
    }

    for (auto& val : jobs_arr.as_array()) {
        i = val.as_object()["id"].as_int64();
        auto job_due = year(val.as_object()["ymd"].as_array()[0].as_int64()) / month(val.as_object()["ymd"].as_array()[1].as_int64())
            / day(val.as_object()["ymd"].as_array()[2].as_int64());
        jobs.emplace_back(make_shared<Job>(val.as_object()["bags"].as_int64(), job_due, i, val.as_object()["features"].as_object()));
    }
}

vector<shared_ptr<Machine>> Sorter::getCompatibleFor(shared_ptr<Job>& job)
{
    vector<shared_ptr<Machine>> ret;
    for (shared_ptr<Machine>& mach : machines)
        if (job->sharesFeatures(*mach)) ret.emplace_back(mach);

    return ret;
}

optional<schedule> Sorter::matchToSchedule()
{
    using compat_list = vector<shared_ptr<Machine>>;
    map<shared_ptr<Job>, compat_list> compat_matrix;

    for (auto& p : jobs)
        compat_matrix[p] = getCompatibleFor(p);

    using Task = shared_ptr<Job>;
    vector<IntVar> ends; // for optimizing makespan
    CpModelBuilder model;
    map<Task, TaskInfo> task_info;

    auto firsthalves = getWorkerAvailability();

    for (Task& g : jobs) {
        auto id = g->getId();
        int minutes = g->getMinutesTo(start_day);

        IntVar start = model.NewIntVar(firsthalves).WithName(string("start_") + to_string(id));
        IntVar end = model.NewIntVar({0, minutes}).WithName(string("end_") + to_string(id));
        ends.push_back(end);

        vector<int64_t> valid_machines;
        for (auto& mach : compat_matrix[g])
            valid_machines.push_back(mach->getId());

        IntVar machine_num = model.NewIntVar(Domain::FromValues(valid_machines)).WithName(string("machnum_") + to_string(id)); // Need specific domain of only compatible
        IntervalVar duration = model.NewIntervalVar(start, static_cast<double>(g->getBags()) / (1000.0 / 60.0) + 10, end); // 1000 bags per hour (for now)

        task_info[g] = {duration, start, end, machine_num};
    }

    for (int i = 0; i < jobs.size(); i++) {
        auto& g = jobs[i];
        auto id1 = g->getId();
        for (int j = i + 1; j < jobs.size(); j++) {
            auto& g2 = jobs[j];
            auto id2 = g2->getId();
            if (id1 == id2) continue;

            auto& info1 = task_info[g];
            auto& info2 = task_info[g2];

            enforceOverlapConstraints(model, info1, info2); // Non overlap condition for same machine!
        }
    }

    auto makespan = model.NewIntVar({0, 12 * 24 * 60});
    model.AddMaxEquality(makespan, ends);
    model.Minimize(makespan); // Checking the minimum takes a lot of time (5SAT NP-complete), so iterate a few steps and use the result after (2 - 5) minutes

    Model md;
    SatParameters parameters;
    parameters.set_search_branching(SatParameters::PSEUDO_COST_SEARCH);
    parameters.set_enumerate_all_solutions(false);
    md.Add(NewSatParameters(parameters));
    md.Add(NewFeasibleSolutionObserver([&](const CpSolverResponse& response) {
        cout << response.status() << endl;
        for (Task& t : jobs) {
            auto& info = task_info[t];
            std::cout << "For job id: " << t->getId() << " start: " << SolutionIntegerValue(response, info.start) << " to " << SolutionIntegerValue(response, info.end) << endl;
        }
    }));
    SolveCpModel(model.Build(), &md);
    /*
    auto response = Solve(model.Build());
    if (response.status() == CpSolverStatus::OPTIMAL || response.status() == CpSolverStatus::FEASIBLE) {
        for (Task& t : jobs) {
            auto& info = task_info[t];
            std::cout << "For job id: " << t->getId() << " start: " << SolutionIntegerValue(response, info.start) << " to " << SolutionIntegerValue(response, info.end) << endl;
        }
    }*/

    // Need to eliminate gaps that were not essential to minimizing makespan
    return nullopt;
}
