#pragma once

#include <boost/json.hpp>

#include <unordered_map>
#include <string>
#include <variant>
#include <chrono>

#include "machine.h"
/**
 * @todo write docs
 */
class Machine;

using namespace std::chrono;
class Job
{
    std::unordered_map<std::string, std::variant<bool, std::pair<int64_t, int64_t>>> features;

    std::chrono::year_month_day due_by;
    int bags;
    int64_t id;
public:
    Job() = delete;
    Job(int bags, std::chrono::year_month_day due_by, int64_t id, boost::json::object feats);

    bool sharesFeatures(Machine& j);

    int64_t getId() {
        return id;
    }

    int getBags() {
        return bags;
    }

    int getMinutesTo(std::chrono::year_month_day today) {
        auto days = sys_days{today};
        auto future_days = sys_days {due_by};

        return (future_days - days).count() * 24 * 60;
    }
};
