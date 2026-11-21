#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace NeoCoreBackend
{
// VM 경계를 넘는 자료는 소유권이 독립적인 숫자 버퍼뿐이다.
struct JobResult
{
    std::int64_t id = 0;
    std::vector<double> floats;
    std::vector<std::int64_t> ints;
    std::vector<std::int32_t> compactInts;
    std::string error;
};

class JobPool
{
public:
    virtual ~JobPool() {}
    virtual int Workers() const = 0;
    virtual void Broadcast(std::string name, std::vector<double> args) = 0;
    virtual std::int64_t Post(std::string name, std::vector<double> args, bool compact) = 0;
    virtual std::vector<JobResult> Run(const std::string& name, std::vector<std::vector<double>> inputs) = 0;
    // 버퍼가 작으면 결과를 보관해 같은 ID를 다시 꺼낼 수 있다. 소비자는 인스턴스 스레드 하나다.
    virtual const JobResult* Peek(bool compact) = 0;
    virtual void Consume(bool compact) = 0;
    virtual const std::string& ErrorText() const = 0;
    virtual std::int64_t Pending() const = 0;
    virtual void Clear() = 0;
    virtual void Close() = 0;
};

class JobFactory
{
public:
    virtual std::shared_ptr<JobPool> Open(const std::string& module, std::int64_t workers) = 0;
protected:
    ~JobFactory() {}
};
}

namespace NeoScript
{
class CNeoVMWorker;
class ValueRef;
namespace JobLibrary
{
bool Open(CNeoVMWorker*, ValueRef, short);
bool Workers(CNeoVMWorker*, ValueRef, short);
bool Broadcast(CNeoVMWorker*, ValueRef, short);
bool Run(CNeoVMWorker*, ValueRef, short);
bool Post(CNeoVMWorker*, ValueRef, short);
bool Post32(CNeoVMWorker*, ValueRef, short);
bool Take(CNeoVMWorker*, ValueRef, short);
bool Take32(CNeoVMWorker*, ValueRef, short);
bool ErrorText(CNeoVMWorker*, ValueRef, short);
bool Pending(CNeoVMWorker*, ValueRef, short);
bool Clear(CNeoVMWorker*, ValueRef, short);
bool Close(CNeoVMWorker*, ValueRef, short);
}
}
