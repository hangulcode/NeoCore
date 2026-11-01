#pragma once

#include "NeoCoreOptions.h"
#include "NeoCoreDiagnostics.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace NeoCore
{

class HostBindings;
class Program;
class Instance;
class Debugger;
struct ObjectHandle;

struct Error
{
    std::string message;
    std::string sourceName;
    int line = 0;
    int column = 0;
    std::vector<CompileDiagnostic> diagnostics;
    bool ok() const { return message.empty(); }
};

// 호스트 경계에서만 쓰는 소유 값이다. VM의 변수 배치나 동적 컴파일 API를 노출하지 않는다.
class Scalar
{
public:
    enum class Kind { Void, Int, Float, Bool, String, Null, Vector2, Vector3, Vector4, Quaternion, Object,
        List, Map, Set, Array, Function, Struct };
    Scalar() = default;
    static Scalar Int(std::int64_t value);
    static Scalar Float(double value);
    static Scalar Bool(bool value);
    static Scalar String(const std::string& value);
    static Scalar Null();
    static Scalar Vector(Kind kind, const float* components);
    static Scalar Object(const std::string& typeName, std::uint64_t id);
    Kind kind() const { return kind_; }
    std::int64_t asInt() const;
    double asFloat() const;
    bool asBool() const;
    const std::string& asString() const;
    const float* asVector() const;
    std::uint64_t asObject() const;
    const std::string& objectType() const;
    std::string typeName() const;
    std::size_t Count() const;
    Scalar At(std::size_t index) const;
    Scalar KeyAt(std::size_t index) const;
    Scalar Get(const Scalar& key) const;
    bool Contains(const Scalar& key) const;
    void Set(std::size_t index, const Scalar& value) const;
    void Put(const Scalar& key, const Scalar& value) const;
    void Append(const Scalar& value) const;
    bool Remove(const Scalar& key) const;
    void Resize(std::size_t count) const;
    void Reserve(std::size_t capacity) const;
    void RemoveAt(std::size_t index) const;
    void ReadArray(std::int64_t* output, std::size_t count, std::size_t offset = 0) const;
    void ReadArray(double* output, std::size_t count, std::size_t offset = 0) const;
    void ReadArray(std::int32_t* output, std::size_t count, std::size_t offset = 0) const;
    void ReadArray(float* output, std::size_t count, std::size_t offset = 0) const;
    void WriteArray(const std::int64_t* input, std::size_t count, std::size_t offset = 0) const;
    void WriteArray(const double* input, std::size_t count, std::size_t offset = 0) const;
    void WriteArray(const std::int32_t* input, std::size_t count, std::size_t offset = 0) const;
    void WriteArray(const float* input, std::size_t count, std::size_t offset = 0) const;
    void Clear() const;
    bool SameStorage(const Scalar& other) const;
    Scalar Clone() const;
private:
    friend struct BoundaryAccess;
    friend class Program;
    friend class Instance;
    struct BoundValue;
    Kind kind_ = Kind::Void;
    std::int64_t integer_ = 0;
    double number_ = 0;
    std::string string_;
    float vector_[4] = {};
    std::uint64_t object_ = 0;
    std::shared_ptr<BoundValue> bound_;
};

// 모든 런타임을 합산한 프로세스 전역 진단 값이다. 프로그램 하나의 사용량이 아니다.
struct AllocationStats
{
    std::int32_t strings = 0;
    std::int32_t maps = 0;
    std::int32_t lists = 0;
    std::int32_t sets = 0;
    std::int32_t coroutines = 0;
    std::int32_t modules = 0;
    std::int32_t asyncs = 0;
    std::int32_t vectors = 0;
    std::int32_t arrays = 0;
    std::int32_t closures = 0;
    std::int64_t bufferBytes = 0;
    std::int64_t poolBytes = 0;
    std::int64_t stringIdleBytes = 0;
};
AllocationStats GetAllocationStats();
// 로그 연결은 프로세스 전체에 적용된다. 실행 중인 다른 스레드와 동시에 변경하지 않는다.
void SetLogHandler(void (*print)(const char*), void (*error)(const char*));

enum class ExecutionStatus { Completed, Suspended, Failed };

struct Result
{
    Error error;
    Scalar value;
    ExecutionStatus status = ExecutionStatus::Completed;
    bool suspended() const { return status == ExecutionStatus::Suspended; }
    bool ok() const { return error.ok(); }
};

// 런타임 하나가 고정된 호스트 계약과 여러 프로그램의 VM 자원을 공유한다.
// 같은 런타임에 속한 프로그램과 인스턴스는 한 스레드에서 사용한다.
class Runtime
{
public:
    static std::unique_ptr<Runtime> Create(Error& error, const CompileOptions& options = CompileOptions());
    static std::unique_ptr<Runtime> Create(const HostBindings& bindings, Error& error,
        const CompileOptions& options = CompileOptions());
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    std::unique_ptr<Program> Compile(const std::string& source, Error& error,
        const std::string& sourceName = "<source>", bool debug = false,
        const CompileOptions& options = CompileOptions());
    std::unique_ptr<Program> LoadImage(const std::vector<std::uint8_t>& image,
        Error& error, const CompileOptions& options = CompileOptions());
    // 반환 포인터는 인스턴스를 소유하지 않는다. 파괴될 수 있는 호출을 거치면 ID로 다시 찾는다.
    Instance* FindInstance(std::uint64_t id) const;
    Debugger* GetDebugger();
    std::int64_t TrimMemory(bool force = false);
    std::int64_t CollectCycles(bool force = false);
    void SetEmptyPageHoldSeconds(float seconds);
    void SetTrimPagesPerCall(int pages);
private:
    friend class Program;
    friend class Instance;
    friend class Debugger;
    struct Impl;
    explicit Runtime(std::shared_ptr<Impl> impl);
    static std::unique_ptr<Runtime> CreateImpl(const HostBindings* bindings, Error& error,
        const CompileOptions& options);
    std::shared_ptr<Impl> impl_;
};

// 컴파일 결과와 바인딩 메타데이터를 공유한다. 각 인스턴스의 전역 값과 실행 상태는 독립적이다.
// 같은 런타임에 속한 인스턴스는 한 스레드에서 순서대로 실행한다.
class Program
{
public:
    static std::unique_ptr<Program> Compile(const std::string& source, Error& error,
        const std::string& sourceName = "<source>", bool debug = false,
        const CompileOptions& options = CompileOptions());
    static std::unique_ptr<Program> Compile(const std::string& source, const HostBindings& bindings,
        Error& error, const std::string& sourceName = "<source>", bool debug = false,
        const CompileOptions& options = CompileOptions());
    // 이미지에는 바이트코드와 정적 타입 계약이 함께 들어간다. 호스트 연결과 객체 ID는 저장하지 않는다.
    bool SaveImage(std::vector<std::uint8_t>& image, Error& error) const;
    static std::unique_ptr<Program> LoadImage(const std::vector<std::uint8_t>& image,
        Error& error, const CompileOptions& options = CompileOptions());
    static std::unique_ptr<Program> LoadImage(const std::vector<std::uint8_t>& image,
        const HostBindings& bindings, Error& error, const CompileOptions& options = CompileOptions());
    ~Program();
    Program(const Program&) = delete;
    Program& operator=(const Program&) = delete;

    std::unique_ptr<Instance> CreateInstance(Error& error);

    // 편의 함수는 프로그램의 기본 인스턴스에 위임한다.
    // 최상위 코드는 한 번만 실행한다. 실패 후 재실행도 하지 않는다.
    Result Run();
    // export의 정적 서명을 검사한다. limits는 함수 본문에 적용하며 선행 전역 초기화에는 적용하지 않는다.
    Result Call(const std::string& name, const std::vector<Scalar>& arguments = {},
        const ExecutionLimits& limits = ExecutionLimits());
    Result Resume(double deltaTime = 0.0, std::uint32_t instructionBudget = 0);
    bool IsSuspended() const;
    bool IsExecuting() const;
    bool Cancel(Error& error);
    bool Reset(Error& error);
    Scalar CreateValue(const std::string& typeName, Error& error);
    Scalar CreateValue(const std::string& typeName, const std::vector<Scalar>& fields, Error& error);
    Scalar FindFunction(const std::string& name, Error& error);
    Scalar ParseJson(const std::string& typeName, const std::string& json, Error& error, bool strictIntegers = true);
    std::string ToJson(const Scalar& value, Error& error, bool sortKeys = false);
    // 전역 객체의 ID만 바꾼다. 다음 멤버 호출에서 호스트가 실제 생존 여부를 검사한다.
    bool SetHostObject(const std::string& name, ObjectHandle object, Error& error);
    // 다른 프로그램도 포함해 공유 런타임 전체의 빈 풀 페이지만 반환한다.
    // force는 보유 시간과 호출당 페이지 예산을 무시하며 살아 있는 값을 해제하지 않는다.
    std::int64_t TrimMemory(bool force = false);
    std::int64_t CollectCycles(bool force = false);
    void SetEmptyPageHoldSeconds(float seconds);
    void SetTrimPagesPerCall(int pages);
private:
    friend class Runtime;
    friend class Instance;
    friend class Debugger;
    friend struct DebugAccess;
    struct Impl;
    static std::unique_ptr<Program> CompileImpl(const std::string& source, Error& error,
        const std::string& sourceName, bool debug, const std::shared_ptr<Runtime::Impl>& runtime, const CompileOptions& options);
    static std::unique_ptr<Program> LoadImageImpl(const std::vector<std::uint8_t>& image,
        Error& error, const std::shared_ptr<Runtime::Impl>& runtime, const CompileOptions& options);
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
    bool IsExecuting() const;
    bool Cancel(Error& error);
    bool Reset(Error& error);
    Result Call(const Scalar& function, const std::vector<Scalar>& arguments = {},
        const ExecutionLimits& limits = ExecutionLimits());
    Result ReadGlobal(const std::string& name);
    Result WriteGlobal(const std::string& name, const Scalar& value);
    bool SetHostObject(const std::string& name, ObjectHandle object, Error& error);
    // ID와 사용자 포인터는 Reset 후에도 유지된다. 지연 이벤트 취소는 호스트가 담당한다.
    std::uint64_t Id() const;
    void SetUserData(void* value);
    void* GetUserData() const;
private:
    friend class Program;
    friend class Runtime;
    friend class Debugger;
    friend struct DebugAccess;
    friend struct BoundaryAccess;
    struct Impl;
    explicit Instance(std::unique_ptr<Impl> impl);
    Result Invoke(const std::string& name, const Scalar* function, const std::vector<Scalar>& arguments,
        const ExecutionLimits& limits);
    std::unique_ptr<Impl> impl_;
};

}
