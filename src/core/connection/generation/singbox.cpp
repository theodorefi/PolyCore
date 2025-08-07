#include "singbox.hpp"
#include "core/connection/Generation.hpp"

namespace Qv2ray::core::connection::generation::singbox
{
    static QJsonObject mapInboundToSingBox(const QJsonObject &in)
    {
        QJsonObject sb;
        const auto proto = in.value("protocol").toString();
        const auto listen = in.value("listen").toString("127.0.0.1");
        const auto port = in.value("port").toInt(0);
        sb["listen"] = listen;
        sb["listen_port"] = port;
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
            // best-effort map to tproxy; sing-box requires separate inbound type "tproxy"/"redirect".
            sb["type"] = "tproxy";
            sb["network"] = "tcp,udp";
        }
        else
        {
            // default to mixed for safety
            sb["type"] = "mixed";
        }
        // simple sniff
        const auto sniff = in.value("sniffing").toObject();
        if (!sniff.isEmpty()) sb["sniff"] = sniff.value("enabled").toBool(false);
        // tag
        sb["tag"] = in.value("tag").toString();
        return sb;
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
            // fallback: try socks detour if exists
            sb["type"] = "direct";
        }
        // TLS
        if (stream.value("security").toString() == "tls")
        {
            QJsonObject tls; tls["enabled"] = true;
            const auto sni = stream.value("tlsSettings").toObject().value("serverName").toString();
            if (!sni.isEmpty()) tls["server_name"] = sni;
            sb["tls"] = tls;
        }
        // Transport (map a few common ones)
        const auto net = stream.value("network").toString();
        if (net == "ws")
        {
            QJsonObject ws; ws["type"] = "ws";
            const auto p = stream.value("wsSettings").toObject().value("path").toString();
            if (!p.isEmpty()) ws["path"] = p;
            const auto host = stream.value("wsSettings").toObject().value("headers").toObject().value("Host").toString();
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
        return sb;
    }

    static QJsonObject buildDNS()
    {
        // sing-box 1.12.0 new DNS format example: udp 1.1.1.1 + system rule
        QJsonObject dns;
        QJsonArray servers;
        servers.append(QJsonObject{ { "type", "udp" }, { "server", "1.1.1.1" } });
        servers.append(QJsonObject{ { "type", "local" }, { "tag", "system" } });
        dns["servers"] = servers;
        QJsonArray rules;
        rules.append(QJsonObject{ { "server", "system" }, { "strategy", "prefer_ipv4" } });
        dns["rules"] = rules;
        dns["strategy"] = "prefer_ipv4";
        return dns;
    }

    static QJsonObject buildRoute(const QString &proxyTag)
    {
        QJsonObject route;
        // private ip direct
        QJsonArray rules;
        rules.append(QJsonObject{ { "ip_is_private", true }, { "outbound", "direct" } });
        // final
        route["rules"] = rules;
        route["final"] = proxyTag;
        // placeholders for rule_sets (user can manage separately)
        route["rule_set"] = QJsonArray{};
        route["auto_detect_interface"] = true;
        return route;
    }

    CONFIGROOT GenerateSingBoxConfig(const CONFIGROOT &unified)
    {
        CONFIGROOT sb;
        // log
        sb["log"] = QJsonObject{ { "level", "info" }, { "timestamp", true } };
        // dns
        sb["dns"] = buildDNS();
        // inbounds: map each
        QJsonArray sbIn;
        for (const auto &inV : unified.value("inbounds").toArray())
        {
            sbIn.append(mapInboundToSingBox(inV.toObject()));
        }
        sb["inbounds"] = sbIn;
        // outbounds: first is proxy, append direct/block
        QJsonArray sbOut;
        const auto firstOut = unified.value("outbounds").toArray().value(0).toObject();
        const auto mapped = mapOutboundToSingBox(firstOut);
        const auto proxyTag = mapped.value("tag").toString("proxy");
        sbOut.append(mapped);
        sbOut.append(QJsonObject{ { "type", "direct" }, { "tag", "direct" } });
        sbOut.append(QJsonObject{ { "type", "block" }, { "tag", "block" } });
        // optional dns-out for hijack
        // sbOut.append(QJsonObject{ { "type", "dns" }, { "tag", "dns-out" } });
        sb["outbounds"] = sbOut;
        // route
        sb["route"] = buildRoute(proxyTag);
        // experimental clash api
        const auto controller = QString("127.0.0.1:%1").arg(GlobalConfig.kernelConfig.statsPort);
        sb["experimental"] = QJsonObject{ { "clash_api", QJsonObject{ { "external_controller", controller } } } };
        return sb;
    }
} // namespace Qv2ray::core::connection::generation::singbox