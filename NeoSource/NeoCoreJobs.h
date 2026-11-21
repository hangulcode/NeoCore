#pragma once
#include <atomic>
#include "NeoCoreOptions.h"
#include "NeoJobLibrary.h"

namespace NeoCore
{
class Program;
// 소유 인스턴스가 리셋되거나 사라지면 다른 인스턴스에 핸들 별칭이 남아 있어도 풀을 닫는다.
class InstanceJobs final : public NeoCoreBackend::JobFactory
{
public:
    InstanceJobs(const CompileOptions& options, const std::string& requester)
        : options_(options), requester_(requester) {}
    ~InstanceJobs() { Close(); }
    std::shared_ptr<NeoCoreBackend::JobPool> Open(const std::string& module, std::int64_t workers) override;
    static std::unique_ptr<Program> LoadWorker(const std::vector<std::uint8_t>& image,
        const std::atomic<bool>& stop, Error& error);
    void Close();
private:
    const CompileOptions& options_;
    const std::string& requester_;
    std::vector<std::weak_ptr<NeoCoreBackend::JobPool>> pools_;
};
}
