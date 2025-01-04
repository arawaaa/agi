#pragma once

#include <boost/json.hpp>

#include <unordered_map>
#include <string>
#include <variant>

#include "job.h"
/**
 * @todo write docs
 */
using range = std::pair<int64_t, int64_t>;
class Machine
{
    std::unordered_map<std::string, std::variant<bool, range>> features;
    int64_t machine_speed; // Bags per hour
    int64_t id;
public:
    /**
     * Default constructor
     */
    Machine() = delete;
    Machine(int64_t speed, int64_t id, boost::json::object feats);

    std::variant<bool, range> getFeature(std::string key);

    int getSpeed()
    {
        return machine_speed;
    }

    int64_t getId()
    {
        return id;
    }
};
