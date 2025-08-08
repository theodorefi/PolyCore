#pragma once
#include "base/Qv2rayBase.hpp"

namespace Qv2ray::core::connection::generation::singbox
{
    // Generate a sing-box 1.12.0 compatible configuration from unified (v2ray-like) CONFIGROOT
    // Minimal coverage: local http/socks inbounds, first outbound (vmess/vless/shadowsocks/trojan),
    // basic DNS (new 1.12 format), basic routing with private ip direct and final = proxy,
    // optional experimental.clash_api from GlobalConfig.kernelConfig.statsPort.
    CONFIGROOT GenerateSingBoxConfig(const CONFIGROOT &unified);
}

using namespace Qv2ray::core::connection::generation::singbox;