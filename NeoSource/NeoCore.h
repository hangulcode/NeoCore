#pragma once

#include "NeoCoreOptions.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace NeoCore
{

class HostBindings;
class Instance;
struct ObjectHandle;

struct Error
{
    std::string message;
    std::string sourceName;
    int line = 0;
    int column = 0;
    bool ok() const { return message.empty(); }
};

// 호스트 경계에서만 쓰는 소유 값이다. VM의 변수 배치나 동적 컴파일 API를 노출하지 않는다.
class Scalar
{
public:
    enum class Kind { Void, Int, Float, Bool, String, Null };
    Scalar() = default;
    static Scalar Int(std::int64_t value);
    static Scalar Float(double value);
    static Scalar Bool(bool value);
    static Scalar String(const std::string& value);
    static Scalar Null();
    Kind kind() const { return kind_; }
    std::int64_t asInt() const;
    double asFloat() const;
    bool asBool() const;
    const std::string& asString() const;
private:
    Kind kind_ = Kind::Void;
    std::int64_t integer_ = 0;
    double number_ = 0;
    std::string string_;
};

enum class ExecutionStatus { Completed, Suspended, Failed };

struct Result
{
    Error error;
    Scalar value;
    ExecutionStatus status = ExecutionStatus::Completed;
    bool suspended() const { return status == ExecutionStatus::Suspended; }
    bool ok() const { return error.ok(); }
};

// 컴파일 결과와 바인딩 메타데이터를 공유한다. 각 인스턴스의 전역 값과 실행 상태는 독립적이다.
// 같은 프로그램에 속한 인스턴스는 한 스레드에서 순서대로 실행한다.
class Program
{
public:
    static std::unique_ptr<Program> Compile(const std::string& source, Error& error,
        const std::string& sourceName = "<source>", bool debug = false,
        const CompileOptions& options = CompileOptions());
    static std::unique_ptr<Program> Compile(const std::string& source, const HostBindings& bindings,
        Error& error, const std::string& sourceName = "<source>", bool debug = false,
        const CompileOptions& options = CompileOptions());
    ~Program();
    Program(const Program&) = delete;
    Program& operator=(const Program&) = delete;

    std::unique_ptr<Instance> CreateInstance(Error& error);

    // 편의 함수는 프로그램의 기본 인스턴스에 위임한다.
    // 최상위 코드는 한 번만 실행한다. 실패 후 재실행도 하지 않는다.
    Result Run();
    // 스칼라 export 서명을 검사한다. limits는 함수 본문에 적용하며 선행 전역 초기화에는 적용하지 않는다.
    Result Call(const std::string& name, const std::vector<Scalar>& arguments = {},
        const ExecutionLimits& limits = ExecutionLimits());
    Result Resume(double deltaTime = 0.0, std::uint32_t instructionBudget = 0);
    bool IsSuspended() const;
    // 전역 객체의 ID만 바꾼다. 다음 멤버 호출에서 호스트가 실제 생존 여부를 검사한다.
    bool SetHostObject(const std::string& name, ObjectHandle object, Error& error);
private:
    friend class Instance;
    struct Impl;
    static std::unique_ptr<Program> CompileImpl(const std::string& source, Error& error,
        const std::string& sourceName, bool debug, const HostBindings* bindings, const CompileOptions& options);
    explicit Program(std::shared_ptr<Impl> impl);
    std::shared_ptr<Impl> impl_;
    std::unique_ptr<Instance> defaultInstance_;
};

// 객체 ID와 스크립트 전역 값, 대기 중 호출은 인스턴스마다 따로 소유한다.
// Program 객체가 먼저 파괴되어도 생성한 인스턴스는 컴파일 결과를 계속 보유한다.
class Instance
{
public:
    ~Instance();
    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;
    Result Run();
    Result Call(const std::string& name, const std::vector<Scalar>& arguments = {},
        const ExecutionLimits& limits = ExecutionLimits());
    // sleep은 실제 경과 시간을 사용한다. deltaTime은 예약된 프레임 시간이며 현재 타이머를 앞당기지 않는다.
    Result Resume(double deltaTime = 0.0, std::uint32_t instructionBudget = 0);
    bool IsSuspended() const;
    bool SetHostObject(const std::string& name, ObjectHandle object, Error& error);
private:
    friend class Program;
    struct Impl;
    explicit Instance(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}
