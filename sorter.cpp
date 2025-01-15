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

void Sorter::arrange(schedule& vecs)
{
    for (auto& [id, vec] : vecs) {
        sort(vec.begin(), vec.end(), [](tuple<int, int, int>& first, tuple<int, int, int>& second) -> bool {
            if (get<1>(first) < get<1>(second)) return true;
            return false;
        });

        sort(downtimes[id].begin(), downtimes[id].end(), [](pair<int, int>& first, pair<int, int>& second) -> bool {
            if (get<1>(first) < get<1>(second)) return true;
            return false;
        });

        int previous = 0;
        int64_t current_idx = 0;
        for (auto& it : vec) {
            while (downtimes[id].size() > current_idx + 1 && get<1>(it) > get<1>(downtimes[id][current_idx]))
                if (get<1>(it) > get<1>(downtimes[id][current_idx + 1]))
                    current_idx++;
                else
                    break;


            if (current_idx < downtimes[id].size() && get<1>(downtimes[id][current_idx]) < get<1>(it))
                previous = max(previous, get<1>(downtimes[id][current_idx]));
            int diff = get<1>(it) - previous;
            get<1>(it) -= diff;
            previous = (get<2>(it) -= diff);
        }
    }
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
    workerAvailability = boost::json::value_to<vector<int64_t>>(obj["available"]);
    auto &downtime = obj["downtime"].as_array();

    for (auto& it : downtime) {
        auto& constraint = it.as_object();

        int64_t mach = constraint["id"].as_int64();
        auto& times = constraint["times"].as_array();

        for (int i = 0; i < times.size(); i += 2)
            downtimes[mach].emplace_back(times.at(i).as_int64(), times.at(i + 1).as_int64());
    }

    auto &ymd_arr = ymd.as_array();
    start_day = year(ymd_arr[0].as_int64()) / month(ymd_arr[1].as_int64()) / day(ymd_arr[2].as_int64());

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

    for (auto& p : jobs) {
        compat_matrix[p] = getCompatibleFor(p);
        if (compat_matrix[p].empty()) return nullopt;
    }

    using Task = shared_ptr<Job>;
    vector<IntVar> ends; // for optimizing makespan
    CpModelBuilder model;
    map<Task, TaskInfo> task_info;

    for (Task& g : jobs) {
        auto id = g->getId();
        int minutes = g->getMinutesTo(start_day);

        IntVar start = model.NewIntVar(Domain::FromFlatIntervals(workerAvailability)).WithName(string("start_") + to_string(id));
        IntVar end = model.NewIntVar({0, minutes}).WithName(string("end_") + to_string(id));
        ends.push_back(end);

        vector<int64_t> valid_machines;
        for (auto& mach : compat_matrix[g]) {
            valid_machines.push_back(mach->getId());
        }

        IntVar length = model.NewIntVar({0, numeric_limits<int>::max()}); // Fully constrained variable
        IntVar machine_num = model.NewIntVar(Domain::FromValues(valid_machines)).WithName(string("machnum_") + to_string(id)); // Need specific domain of only compatible

        for (auto& mach : compat_matrix[g]) {
            BoolVar inter = model.NewBoolVar();
            model.AddEquality(machine_num, mach->getId()).OnlyEnforceIf(inter);
            model.AddNotEqual(machine_num, mach->getId()).OnlyEnforceIf(~inter);

            model.AddEquality(length, static_cast<double>(g->getBags()) / (static_cast<double>(mach->getSpeed()) / 60.0))
                .OnlyEnforceIf(inter);

                if (downtimes.contains(mach->getId())) {
                    vector<BoolVar> no_overlaps;

                    for (auto& [dtime_s, dtime_e] : downtimes[mach->getId()]) {
                        auto disjoint = model.NewBoolVar();
                        auto part1 = model.NewBoolVar();
                        auto part2 = model.NewBoolVar();

                        model.AddGreaterOrEqual(dtime_s, end).OnlyEnforceIf(part1);
                        model.AddLessThan(dtime_s, end).OnlyEnforceIf(~part1);
                        model.AddLessOrEqual(dtime_e, start).OnlyEnforceIf(part2);
                        model.AddGreaterThan(dtime_e, start).OnlyEnforceIf(~part2);

                        model.AddBoolOr({part1, part2}).OnlyEnforceIf(disjoint);
                        model.AddBoolAnd({part1.Not(), part2.Not()}).OnlyEnforceIf(~disjoint);
                        no_overlaps.push_back(disjoint);
                    }

                    model.AddBoolAnd(no_overlaps).OnlyEnforceIf(inter);
                }

        }

        IntervalVar duration = model.NewIntervalVar(start, length, end); // 1000 bags per hour (for now)

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
    parameters.set_max_time_in_seconds(200.0);
    md.Add(NewSatParameters(parameters));

    const CpSolverResponse response = SolveCpModel(model.Build(), &md);

    schedule ret;
    if (response.status() == CpSolverStatus::OPTIMAL || response.status() == CpSolverStatus::FEASIBLE) {
        for (Task& t : jobs) {
            auto& info = task_info[t];
            ret[SolutionIntegerValue(response, info.machine_num)].emplace_back(t->getId(), SolutionIntegerValue(response, info.start), SolutionIntegerValue(response, info.end));
        }
    } else {
        return nullopt;
    }

    if (ret.empty()) return nullopt;

    // Need to eliminate gaps that were not essential to minimizing makespan
    arrange(ret);
    return ret;
}
