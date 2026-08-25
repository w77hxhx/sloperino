// SPDX-License-Identifier: MIT

#include "providers/limerino/nuke/NukePlan.hpp"

namespace pajlada {

rapidjson::Value Serialize<chatterino::limerino::NukeTarget>::get(
    const chatterino::limerino::NukeTarget &value,
    rapidjson::Document::AllocatorType &a)
{
    rapidjson::Value ret(rapidjson::kObjectType);
    chatterino::rj::set(ret, "userId", value.userId, a);
    chatterino::rj::set(ret, "login", value.login, a);
    chatterino::rj::set(ret, "displayName", value.displayName, a);
    return ret;
}

chatterino::limerino::NukeTarget
    Deserialize<chatterino::limerino::NukeTarget>::get(
        const rapidjson::Value &value, bool *error)
{
    chatterino::limerino::NukeTarget t;
    if (!value.IsObject())
    {
        PAJLADA_REPORT_ERROR(error)
        return t;
    }
    chatterino::rj::getSafe(value, "userId", t.userId);
    chatterino::rj::getSafe(value, "login", t.login);
    chatterino::rj::getSafe(value, "displayName", t.displayName);
    return t;
}

}  // namespace pajlada
