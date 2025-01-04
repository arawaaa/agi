#include "machine.h"

using namespace std;

Machine::Machine(int64_t speed, int64_t id, boost::json::object feats)
    : machine_speed(speed), id(id)
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

std::variant<bool, range> Machine::getFeature(std::string key)
{
    return features[key];
}
