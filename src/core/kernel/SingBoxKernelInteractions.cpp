#include "SingBoxKernelInteractions.hpp"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTimer>

namespace Qv2ray::core::kernel
{
    static QString getSingBoxPath()
    {
        // Reuse kernelConfig.V2CorePath fields as generic kernel path storage for now.
        return GlobalConfig.kernelConfig.KernelPath();
    }

    SingBoxKernelInstance::SingBoxKernelInstance(QObject *parent) : QObject(parent)
    {
        sProcess = new QProcess(this);
        connect(sProcess, &QProcess::readyReadStandardOutput, this, [&]() {
            emit OnProcessOutputReadyRead(QString::fromUtf8(sProcess->readAllStandardOutput()).trimmed());
        });
        connect(sProcess, &QProcess::readyReadStandardError, this, [&]() {
            emit OnProcessOutputReadyRead(QString::fromUtf8(sProcess->readAllStandardError()).trimmed());
        });
        connect(sProcess, &QProcess::stateChanged, this, [&](QProcess::ProcessState state) {
            if (kernelStarted && state == QProcess::NotRunning)
            {
                kernelStarted = false;
                emit OnProcessErrored("sing-box kernel exited.");
            }
        });
        network = new QNetworkAccessManager(this);
        statsTimer = new QTimer(this);
        statsTimer->setInterval(1000);
        connect(statsTimer, &QTimer::timeout, this, &SingBoxKernelInstance::startStatsIfEnabled);
    }

    SingBoxKernelInstance::~SingBoxKernelInstance()
    {
        StopConnection();
    }

    std::pair<bool, std::optional<QString>> SingBoxKernelInstance::ValidateKernel(const QString &sbPath)
    {
        QProcess proc;
        proc.setProcessChannelMode(QProcess::MergedChannels);
        proc.start(sbPath.isEmpty() ? getSingBoxPath() : sbPath, { "version" }, QIODevice::ReadOnly);
        proc.waitForFinished(8000);
        const auto exitCode = proc.exitCode();
        const auto output = QString::fromUtf8(proc.readAll());
        if (exitCode == 0 && output.contains("sing-box"))
        {
            return { true, output.split('\n').value(0) };
        }
        return { false, QString("Failed to execute sing-box: %1").arg(output) };
    }

    std::optional<QString> SingBoxKernelInstance::ValidateConfig(const QString &path)
    {
        const auto sbPath = getSingBoxPath();
        QProcess proc;
        proc.setProcessChannelMode(QProcess::MergedChannels);
        proc.start(sbPath, { "check", "-c", path }, QIODevice::ReadOnly);
        proc.waitForFinished(10000);
        if (proc.exitCode() != 0)
        {
            const auto out = QString::fromUtf8(proc.readAll());
            return out.isEmpty() ? QString("sing-box check failed") : out;
        }
        return std::nullopt;
    }

    QString SingBoxKernelInstance::writeRuntimeConfigFile(const CONFIGROOT &root) const
    {
        // Write to config dir/runtime/singbox.json
        const auto dir = QV2RAY_CONFIG_DIR + "runtime/";
        QDir().mkpath(dir);
        const auto path = dir + "singbox.json";
        QFile f(path);
        if (f.open(QFile::WriteOnly | QFile::Truncate))
        {
            f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
            f.close();
        }
        return path;
    }

    std::optional<QString> SingBoxKernelInstance::StartConnection(const CONFIGROOT &root)
    {
        StopConnection();
        const auto sbPath = getSingBoxPath();
        const auto cfgPath = writeRuntimeConfigFile(root);

        // Detect Clash API setting from config.experimental.clash_api
        const auto exp = root.value("experimental").toObject();
        const auto clash_api = exp.value("clash_api").toObject();
        externalController = clash_api.value("external_controller").toString();
        apiSecret = clash_api.value("secret").toString();
        apiEnabled = !externalController.isEmpty();

        // Optional: set work dir to config dir to make relative paths work
        sProcess->setWorkingDirectory(QV2RAY_CONFIG_DIR);
        // Disable ANSI color for cleaner logs
        QStringList args{ "run", "-c", cfgPath, "--disable-color" };
        sProcess->start(sbPath, args, QIODevice::ReadWrite | QIODevice::Text);
        if (!sProcess->waitForStarted(5000))
        {
            return QString("Failed to start sing-box");
        }
        kernelStarted = true;

        // Start stats poller (Clash API) if enabled
        if (apiEnabled)
        {
            statsTimer->start();
        }
        return std::nullopt;
    }

    void SingBoxKernelInstance::startStatsIfEnabled()
    {
        if (!apiEnabled || !kernelStarted || externalController.isEmpty()) return;
        // Minimal stats: GET /traffic or /proxies if implemented by sing-box Clash API
        // Here we try /traffic first; fallback to aggregate via /proxies if needed.
        const QUrl url(QString("http://%1/traffic").arg(externalController));
        QNetworkRequest req(url);
        if (!apiSecret.isEmpty()) req.setRawHeader("Authorization", QByteArray("Bearer ") + apiSecret.toUtf8());
        auto *reply = network->get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            QMap<StatisticsType, QvStatsSpeed> stats;
            do
            {
                if (reply->error() != QNetworkReply::NoError) break;
                const auto body = reply->readAll();
                const auto json = QJsonDocument::fromJson(body).object();
                // Expect fields like { "upload": n, "download": n } or similar per Clash API implementations
                const auto up = json.value("upload").toVariant().toLongLong();
                const auto down = json.value("download").toVariant().toLongLong();
                if (up || down)
                {
                    stats[API_OUTBOUND_PROXY] = { up, down };
                }
            } while (false);
            reply->deleteLater();
            if (!stats.empty()) emit OnNewStatsDataArrived(stats);
        });
    }

    void SingBoxKernelInstance::StopConnection()
    {
        if (statsTimer) statsTimer->stop();
        if (sProcess && sProcess->state() != QProcess::NotRunning)
        {
            sProcess->terminate();
            if (!sProcess->waitForFinished(2000))
            {
                sProcess->kill();
                sProcess->waitForFinished(1000);
            }
        }
        kernelStarted = false;
    }
} // namespace Qv2ray::core::kernel