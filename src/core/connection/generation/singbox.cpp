#include "singbox.hpp"
#include "core/connection/Generation.hpp"

namespace Qv2ray::core::connection::generation::singbox
{
    static QString mapDomainStrategy(const QString &s)
    {
        // pass-through supported sing-box strategies
        if (s == "prefer_ipv4" || s == "prefer_ipv6" || s == "ipv4_only" || s == "ipv6_only") return s;
        return "prefer_ipv4";
    }

    static QJsonObject mapInboundToSingBox(const QJsonObject &in)
    {
        QJsonObject sb;
        const auto proto = in.value("protocol").toString();
        const auto listen = in.value("listen").toString("127.0.0.1");
        const auto port = in.value("port").toInt(0);
        sb["listen"] = listen;
        if (port > 0) sb["listen_port"] = port;
        if (proto == "http")
        {
            sb["type"] = "http";
        }
        else if (proto == "socks")
        {
            sb["type"] = "socks";
            const auto settings = in.value("settings").toObject();
            if (settings.value("udp").toBool(false)) sb["udp"] = true;
        }
        else if (proto == "dokodemo-door")
        {
            // map to tproxy; enable auto_redirect for better compatibility
            sb["type"] = "tproxy";
            sb["network"] = "tcp,udp";
            sb["auto_redirect"] = true;
        }
        else if (proto == "tun")
        {
            sb["type"] = "tun";
            // minimal fields; advanced route address/rules need separate UI
            sb["auto_route"] = true;
            sb["stack"] = "system";
        }
        else if (proto == "redirect")
        {
            sb["type"] = "redirect";
            sb["network"] = "tcp";
        }
        else
        {
            sb["type"] = "mixed";
        }
        const auto sniff = in.value("sniffing").toObject();
        if (!sniff.isEmpty()) sb["sniff"] = sniff.value("enabled").toBool(false);
        sb["tag"] = in.value("tag").toString();
        return sb;
    }

    static void applyTLSAndTransport(QJsonObject &sb, const QJsonObject &stream)
    {
        if (stream.value("security").toString() == "tls")
        {
            QJsonObject tls; tls["enabled"] = true;
            const auto tlsSettings = stream.value("tlsSettings").toObject();
            const auto sni = tlsSettings.value("serverName").toString();
            if (!sni.isEmpty()) tls["server_name"] = sni;
            // utls default to chrome if unspecified
            const auto fp = tlsSettings.value("fingerprint").toString("chrome");
            tls["utls"] = QJsonObject{ { "enabled", true }, { "fingerprint", fp } };
            // reality
            const auto reality = tlsSettings.value("reality").toObject();
            if (!reality.isEmpty())
            {
                QJsonObject r; r["enabled"] = true; r["public_key"] = reality.value("public_key").toString();
                const auto sid = reality.value("short_id").toString(); if (!sid.isEmpty()) r["short_id"] = sid;
                tls["reality"] = r;
                // vision flow default for VLESS
                if (sb.value("type").toString() == "vless" && sb.value("flow").toString().isEmpty())
                    sb["flow"] = "xtls-rprx-vision";
            }
            sb["tls"] = tls;
        }
        // Transport
        const auto net = stream.value("network").toString();
        if (net == "ws")
        {
            QJsonObject ws; ws["type"] = "ws";
            const auto wso = stream.value("wsSettings").toObject();
            const auto p = wso.value("path").toString();
            if (!p.isEmpty()) ws["path"] = p;
            const auto host = wso.value("headers").toObject().value("Host").toString();
            if (!host.isEmpty()) ws["headers"] = QJsonObject{ { "Host", host } };
            sb["transport"] = ws;
        }
        else if (net == "grpc")
        {
            QJsonObject g; g["type"] = "grpc";
            const auto svc = stream.value("grpcSettings").toObject().value("serviceName").toString();
            if (!svc.isEmpty()) g["service_name"] = svc;
            sb["transport"] = g;
        }
        else if (net == "http")
        {
            QJsonObject h2; h2["type"] = "http";
            sb["transport"] = h2;
        }
    }

    static QJsonObject mapOutboundToSingBox(const QJsonObject &out)
    {
        QJsonObject sb;
        const auto proto = out.value("protocol").toString();
        const auto tag = out.value("tag").toString("proxy");
        const auto settings = out.value("settings").toObject();
        const auto stream = out.value("streamSettings").toObject();
        sb["tag"] = tag;
        if (proto == "vmess")
        {
            sb["type"] = "vmess";
            const auto vnext = settings.value("vnext").toArray().value(0).toObject();
            const auto add = vnext.value("address").toString();
            const auto port = vnext.value("port").toInt();
            const auto user = vnext.value("users").toArray().value(0).toObject();
            sb["server"] = add;
            sb["server_port"] = port;
            sb["uuid"] = user.value("id").toString();
        }
        else if (proto == "vless")
        {
            sb["type"] = "vless";
            const auto vnext = settings.value("vnext").toArray().value(0).toObject();
            sb["server"] = vnext.value("address").toString();
            sb["server_port"] = vnext.value("port").toInt();
            const auto user = vnext.value("users").toArray().value(0).toObject();
            sb["uuid"] = user.value("id").toString();
            const auto flow = user.value("flow").toString();
            if (!flow.isEmpty()) sb["flow"] = flow;
        }
        else if (proto == "shadowsocks")
        {
            sb["type"] = "shadowsocks";
            const auto servers = settings.value("servers").toArray().value(0).toObject();
            sb["server"] = servers.value("address").toString();
            sb["server_port"] = servers.value("port").toInt();
            sb["method"] = servers.value("method").toString();
            sb["password"] = servers.value("password").toString();
        }
        else if (proto == "trojan")
        {
            sb["type"] = "trojan";
            const auto servers = settings.value("servers").toArray().value(0).toObject();
            sb["server"] = servers.value("address").toString();
            sb["server_port"] = servers.value("port").toInt();
            sb["password"] = servers.value("password").toString();
        }
        else if (proto == "freedom")
        {
            sb["type"] = "direct";
        }
        else if (proto == "blackhole")
        {
            sb["type"] = "block";
        }
        else
        {
            sb["type"] = "direct";
        }
        applyTLSAndTransport(sb, stream);
        // per-outbound domain_resolver (1.12 推荐)
        sb["domain_resolver"] = QJsonObject{ { "server", "system" }, { "strategy", mapDomainStrategy("") } };
        return sb;
    }

    static QJsonObject buildDNS(bool enableFakeIP)
    {
        QJsonObject dns;
        QJsonArray servers;
        servers.append(QJsonObject{ { "type", "udp" }, { "server", "1.1.1.1" } });
        servers.append(QJsonObject{ { "type", "local" }, { "tag", "system" } });
        // add minimal resolved server for systemd-resolved integrations
        servers.append(QJsonObject{ { "type", "resolved" }, { "tag", "resolved" } });
        if (enableFakeIP)
        {
            servers.append(QJsonObject{ { "type", "fakeip" }, { "tag", "fakeip" }, { "inet4_range", "198.18.0.0/15" }, { "inet6_range", "fc00::/18" } });
        }
        dns["servers"] = servers;
        QJsonArray rules;
        if (enableFakeIP)
        {
            rules.append(QJsonObject{ { "query_type", QJsonArray{ "A", "AAAA" } }, { "server", "fakeip" } });
        }
        rules.append(QJsonObject{ { "server", "system" }, { "strategy", "prefer_ipv4" } });
        dns["rules"] = rules;
        dns["strategy"] = "prefer_ipv4";
        return dns;
    }

    static QJsonObject buildRuleSets()
    {
        // Provide default remote rule_sets for CN; users may manage cache separately.
        QJsonArray ruleSets;
        ruleSets.append(QJsonObject{ { "tag", "geosite-cn" }, { "type", "remote" }, { "format", "binary" },
                                     { "url", "https://raw.githubusercontent.com/SagerNet/sing-geosite/rule-set/geosite-cn.srs" },
                                     { "download_detour", "direct" } });
        ruleSets.append(QJsonObject{ { "tag", "geoip-cn" }, { "type", "remote" }, { "format", "binary" },
                                     { "url", "https://raw.githubusercontent.com/SagerNet/sing-geoip/rule-set/geoip-cn.srs" },
                                     { "download_detour", "direct" } });
        QJsonObject route; route["rule_set"] = ruleSets; return route;
    }

    static QJsonObject buildRoute(const QString &finalTag, bool addRuleSets)
    {
        QJsonObject route;
        QJsonArray rules;
        rules.append(QJsonObject{ { "ip_is_private", true }, { "outbound", "direct" } });
        route["rules"] = rules;
        route["final"] = finalTag;
        route["auto_detect_interface"] = true;
        if (addRuleSets)
        {
            route["rule_set"] = buildRuleSets().value("rule_set").toArray();
        }
        return route;
    }

    CONFIGROOT GenerateSingBoxConfig(const CONFIGROOT &unified)
    {
        CONFIGROOT sb;
        // log
        sb["log"] = QJsonObject{ { "level", "info" }, { "timestamp", true } };
        // dns (enable fakeip if inbound sniffing requests it)
        bool enableFakeIP = false;
        for (const auto &inV : unified.value("inbounds").toArray())
        {
            const auto sniff = inV.toObject().value("sniffing").toObject();
            const auto overrides = sniff.value("destOverride").toArray();
            for (const auto &ov : overrides) if (ov.toString().contains("fakedns")) enableFakeIP = true;
        }
        sb["dns"] = buildDNS(enableFakeIP);
        // minimal resolved service enablement
        sb["service"] = QJsonObject{ { "resolved", QJsonObject{} } };
        // inbounds
        QJsonArray sbIn;
        for (const auto &inV : unified.value("inbounds").toArray())
        {
            sbIn.append(mapInboundToSingBox(inV.toObject()));
        }
        sb["inbounds"] = sbIn;
        // outbounds & aggregation
        QJsonArray sbOut; QStringList proxyTags;
        for (const auto &outV : unified.value("outbounds").toArray())
        {
            auto mapped = mapOutboundToSingBox(outV.toObject());
            if (mapped.value("type").toString() != "direct" && mapped.value("type").toString() != "block") proxyTags << mapped.value("tag").toString("proxy");
            sbOut.append(mapped);
        }
        QString finalTag = proxyTags.value(0, "proxy");
        if (proxyTags.size() > 1)
        {
            QJsonObject urltest; urltest["type"] = "urltest"; urltest["tag"] = "auto";
            QJsonArray items; for (const auto &t : proxyTags) items.append(t); urltest["outbounds"] = items;
            urltest["url"] = "http://www.gstatic.com/generate_204"; urltest["interval"] = "300s"; urltest["tolerance"] = 50;
            sbOut.append(urltest); finalTag = "auto";
        }
        // essentials
        sbOut.append(QJsonObject{ { "type", "direct" }, { "tag", "direct" } });
        sbOut.append(QJsonObject{ { "type", "block" }, { "tag", "block" } });
        sb["outbounds"] = sbOut;
        // route with rule_sets
        sb["route"] = buildRoute(finalTag, true);
        // default_domain_resolver per 1.12 migration
        QJsonObject defResolver; defResolver["server"] = "system";
        defResolver["strategy"] = mapDomainStrategy(unified.value("routing").toObject().value("domainStrategy").toString());
        auto routeObj = sb.value("route").toObject(); routeObj["default_domain_resolver"] = defResolver; sb["route"] = routeObj;
        // experimental (clash_api + cache_file)
        const auto controller = QString("127.0.0.1:%1").arg(GlobalConfig.kernelConfig.statsPort);
        QJsonObject experimental; experimental["clash_api"] = QJsonObject{ { "external_controller", controller } };
        experimental["cache_file"] = QJsonObject{ { "enabled", true }, { "path", QV2RAY_CONFIG_DIR + "cache.db" }, { "store_fakeip", enableFakeIP } };
        sb["experimental"] = experimental;
        return sb;
    }
} // namespace Qv2ray::core::connection::generation::singbox