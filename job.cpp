#include "job.h"

using namespace std;
using namespace boost::json;
Job::Job(int bags, std::chrono::year_month_day due_by, int64_t id, object feats)
    : due_by(due_by), bags(bags), id(id)
{
    for (auto& kv : feats) {
        bool* val;
        boost::json::array* ow;
        if ((val = kv.value().if_bool())) {
            features[kv.key()] = *val;
        } else if ((ow = kv.value().if_array()) && ow->size() == 2) {
            auto& val1 = (*ow)[0];
            auto& val2 = (*ow)[1];

            int64_t *one, *two;
            one = val1.if_int64();
            two = val2.if_int64();

            if (!one || !two) continue;
            features[kv.key()] = pair {*one, *two};
        }
    }
}

bool Job::sharesFeatures(Machine& j)
{
    using range = pair<int64_t, int64_t>;
    for (auto kv : features) {
        if (holds_alternative<bool>(kv.second) && !get<bool>(kv.second)) continue;

        if (holds_alternative<bool>(kv.second)) {
            auto bool_or_range = j.getFeature(kv.first);
            if (holds_alternative<bool>(bool_or_range) && (!holds_alternative<bool>(kv.second) || !get<bool>(bool_or_range)))
                return false;
            if (holds_alternative<range>(bool_or_range)) {
                if (!holds_alternative<range>(kv.second)) return false;

                auto jobrange = get<range>(kv.second);
                auto machinerange = get<range>(bool_or_range);

                if (jobrange.first < machinerange.first || jobrange.first > machinerange.second ||
                    jobrange.second < machinerange.first || jobrange.second > machinerange.second
                )
                    return false;
            }
        }
    }
    return true;
}
