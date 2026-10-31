#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace NeoCore
{

class HostBindings;
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

struct Result
{
    Error error;
    Scalar value;
    bool ok() const { return error.ok(); }
};

// 초기 전환 단계에서는 타입 검사를 통과한 소스만 내부 VM으로 내린다.
// 인스턴스 하나를 소유하며 동일 객체의 병렬 호출은 지원하지 않는다.
class Program
{
public:
    static std::unique_ptr<Program> Compile(const std::string& source, Error& error,
        const std::string& sourceName = "<source>", bool debug = false);
    static std::unique_ptr<Program> Compile(const std::string& source, const HostBindings& bindings,
        Error& error, const std::string& sourceName = "<source>", bool debug = false);
    ~Program();
    Program(const Program&) = delete;
    Program& operator=(const Program&) = delete;

    // 최상위 코드는 한 번만 실행한다. 실패 후 재실행도 하지 않는다.
    Result Run();
    // 스칼라를 반환하는 export 함수만 호출하며 실행 전에 서명과 인자 타입·개수를 검사한다.
    Result Call(const std::string& name, const std::vector<Scalar>& arguments = {});
    // 전역 객체의 ID만 바꾼다. 다음 멤버 호출에서 호스트가 실제 생존 여부를 검사한다.
    bool SetHostObject(const std::string& name, ObjectHandle object, Error& error);
private:
    friend struct HostRuntimeAccess;
    struct Impl;
    static std::unique_ptr<Program> CompileImpl(const std::string& source, Error& error,
        const std::string& sourceName, bool debug, const HostBindings* bindings);
    explicit Program(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}
