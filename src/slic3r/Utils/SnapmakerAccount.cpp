#include "SnapmakerAccount.hpp"

#include <nlohmann/json.hpp>

namespace Slic3r {

using json = nlohmann::json;

namespace {
// Decode one base64url segment (no padding) to bytes. Returns false on any
// invalid character or length so callers can treat it as "not decodable".
bool base64url_decode(const std::string& in, std::string& out)
{
    static const std::string tbl =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    out.clear();
    int val = 0, bits = -8;
    for (unsigned char c : in) {
        auto pos = tbl.find(c);
        if (pos == std::string::npos)
            return false; // '=' padding or any non-alphabet char -> reject
        val = (val << 6) + static_cast<int>(pos);
        bits += 6;
        if (bits >= 0) {
            out.push_back(static_cast<char>((val >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return true;
}
} // namespace

bool sm_parse_account_response(const std::string& body, SMAccountProfile& profile, bool& auth_rejected)
{
    auth_rejected = false;
    auto str_or_empty = [](const json& j, const char* key) {
        return j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : std::string();
    };
    try {
        json response = json::parse(body);
        int  code     = -1;
        if (response.contains("code")) {
            const json& jcode = response["code"];
            if (jcode.is_number_integer())
                code = jcode.get<int>();
            else if (jcode.is_string()) {
                try {
                    code = std::stoi(jcode.get<std::string>());
                } catch (std::exception&) {}
            }
        }
        auth_rejected = code == SM_API_CODE_AUTHORIZATION_MISSING ||
                        code == SM_API_CODE_TOKEN_EXPIRED ||
                        code == SM_API_CODE_AUTHENTICATION_FAILED;
        if (!response.contains("data") || !response["data"].is_object())
            return false;
        const json& data = response["data"];
        if (data.contains("id")) {
            const json& jid = data["id"];
            if (jid.is_number_integer())
                profile.id = std::to_string(jid.get<long long>());
            else if (jid.is_string())
                profile.id = jid.get<std::string>();
        }
        profile.nickname = str_or_empty(data, "nickname");
        profile.icon     = str_or_empty(data, "icon");
        profile.account  = str_or_empty(data, "account");
        return code == SM_API_CODE_OK;
    } catch (std::exception&) {
        return false;
    }
}

std::string sm_account_api_base(const std::string& country_code)
{
    return country_code == "CN" ? "https://api.snapmaker.cn" : "https://id.snapmaker.com";
}

long long sm_token_expiry(const std::string& jwt)
{
    const auto d1 = jwt.find('.');
    if (d1 == std::string::npos)
        return 0;
    const auto d2 = jwt.find('.', d1 + 1);
    if (d2 == std::string::npos)
        return 0;
    const std::string payload_b64 = jwt.substr(d1 + 1, d2 - d1 - 1);
    std::string payload;
    if (!base64url_decode(payload_b64, payload))
        return 0;
    try {
        json j = json::parse(payload);
        if (!j.contains("exp"))
            return 0;
        const json& e = j["exp"];
        if (e.is_number_integer())
            return e.get<long long>();
        if (e.is_string())
            return std::stoll(e.get<std::string>());
    } catch (std::exception&) {
        return 0;
    }
    return 0;
}

} // namespace Slic3r
