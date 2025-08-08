#pragma once
#include "base/Qv2rayBase.hpp"

class QProcess;
class QTimer;
class QNetworkAccessManager;

namespace Qv2ray::core::kernel
{
    class SingBoxKernelInstance : public QObject
    {
        Q_OBJECT
      public:
        explicit SingBoxKernelInstance(QObject *parent = nullptr);
        ~SingBoxKernelInstance() override;
        // Start sing-box with provided configuration JSON (CONFIGROOT expected to be sing-box JSON or pre-translated)
        std::optional<QString> StartConnection(const CONFIGROOT &root);
        void StopConnection();
        bool IsKernelRunning() const { return kernelStarted; }

        // Validate config and kernel
        static std::optional<QString> ValidateConfig(const QString &path);
        static std::pair<bool, std::optional<QString>> ValidateKernel(const QString &sbPath);

      signals:
        void OnProcessErrored(const QString &errMessage);
        void OnProcessOutputReadyRead(const QString &output);
        void OnNewStatsDataArrived(const QMap<StatisticsType, QvStatsSpeed> &data);

      private:
        QString writeRuntimeConfigFile(const CONFIGROOT &root) const;
        void startStatsIfEnabled();

      private:
        QProcess *sProcess = nullptr;
        bool kernelStarted = false;
        bool apiEnabled = false;
        QString externalController; // host:port for Clash API
        QString apiSecret;
        QTimer *statsTimer = nullptr;
        QNetworkAccessManager *network = nullptr;
    };
} // namespace Qv2ray::core::kernel

using namespace Qv2ray::core::kernel;