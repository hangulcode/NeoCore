#pragma once

#include "NeoCore.h"
#include "NeoCoreHostSchema.h"
#include <functional>
#include <memory>
#include <vector>

namespace NeoCore
{

// ID의 발급·재사용·수명은 호스트가 관리한다. VM은 객체를 소유하지 않는다.
struct ObjectHandle
{
    std::uint64_t value = 0;
    ObjectHandle() = default;
    explicit ObjectHandle(std::uint64_t id) : value(id) {}
    explicit operator bool() const { return value != 0; }
};

// 벡터는 포인터 대신 성분을 복사한다. 쿼터니언은 기존 백엔드와 같은 w,x,y,z 순서다.
struct Vector2Value
{
    float x, y;
    Vector2Value(float xValue = 0, float yValue = 0) : x(xValue), y(yValue) {}
};
struct Vector3Value
{
    float x, y, z;
    Vector3Value(float xValue = 0, float yValue = 0, float zValue = 0) : x(xValue), y(yValue), z(zValue) {}
};
struct Vector4Value
{
    float x, y, z, w;
    Vector4Value(float xValue = 0, float yValue = 0, float zValue = 0, float wValue = 0)
        : x(xValue), y(yValue), z(zValue), w(wValue) {}
};
struct QuaternionValue
{
    float w, x, y, z;
    QuaternionValue(float wValue = 1, float xValue = 0, float yValue = 0, float zValue = 0)
        : w(wValue), x(xValue), y(yValue), z(zValue) {}
};

class HostCall;
typedef std::function<bool(HostCall&)> HostFunction;
typedef void* (*HostResolver)(void* userData, TypeId type, ObjectHandle object);

// 컴파일용 정의와 실행 연결을 분리한다. Program은 연결 정보를 복사하여 보관한다.
class HostBindings
{
public:
    HostBindings(const HostSchema& schema, HostResolver resolver, void* userData = nullptr);
    const HostSchema& Schema() const { return schema_; }
    bool Bind(HostBindingId binding, HostFunction function);
    bool BindGlobal(const std::string& name, ObjectHandle object);
    bool Validate(std::string& error) const;
    const std::string& LastError() const { return error_; }
private:
    friend struct HostRuntimeAccess;
    HostSchema schema_;
    HostResolver resolver_ = nullptr;
    void* userData_ = nullptr;
    std::vector<HostFunction> functions_;
    std::vector<ObjectHandle> globals_;
    std::vector<bool> globalBound_;
    std::string error_;
};

// 인자형은 컴파일된 서명으로 고정된다. 문자열과 ID는 재진입 전에 독립적으로 보관한다.
// Receiver/Object가 주는 포인터는 빌린 값이며 재진입·외부 삭제 이후 보관해서 쓰지 않는다.
class HostCall
{
public:
    ~HostCall();
    HostCall(const HostCall&) = delete;
    HostCall& operator=(const HostCall&) = delete;

    void* UserData() const;
    // 수신 객체와 별개인 현재 호출 인스턴스다. 지연 이벤트에는 참조 대신 Id()를 보관한다.
    Instance& CallerInstance() const;
    void* InstanceUserData() const;
    ObjectHandle ReceiverId() const;
    void* Receiver();
    // 호스트가 별도 콜백이나 삭제 경로를 실행했다면 다시 조회한다.
    void* ResolveReceiver();
    std::size_t ArgumentCount() const;
    bool IsNull(std::size_t index) const;
    Scalar Argument(std::size_t index) const;
    std::int64_t Int(std::size_t index) const;
    double Float(std::size_t index) const;
    bool Bool(std::size_t index) const;
    const std::string& String(std::size_t index) const;
    Vector2Value Vector2(std::size_t index) const;
    Vector3Value Vector3(std::size_t index) const;
    Vector4Value Vector4(std::size_t index) const;
    QuaternionValue Quaternion(std::size_t index) const;
    ObjectHandle ObjectId(std::size_t index) const;
    void* Object(std::size_t index);
    void* ResolveObject(std::size_t index);

    void ReturnInt(std::int64_t value);
    void ReturnFloat(double value);
    void ReturnBool(bool value);
    void ReturnString(const std::string& value);
    void ReturnVector2(Vector2Value value);
    void ReturnVector3(Vector3Value value);
    void ReturnVector4(Vector4Value value);
    void ReturnQuaternion(QuaternionValue value);
    void ReturnObject(ObjectHandle value);
    void ReturnNull();
    void ReturnValue(const Scalar& value);
    // Out 매개변수 쓰기. 스키마에 Out 으로 등록한 매개변수에만 쓸 수 있고, 호출이 성공하면 스크립트의 인자 변수에
    // 대입된다. 쓰지 않은 Out 매개변수의 변수는 호출 전 값을 유지한다. 현재 값은 일반 인자처럼 읽을 수 있다.
    // Out 이 아닌 매개변수에 쓰거나 선언 타입과 다른 값을 쓰면 호출이 실패한다.
    bool IsOut(std::size_t index) const;
    void OutInt(std::size_t index, std::int64_t value);
    void OutFloat(std::size_t index, double value);
    void OutBool(std::size_t index, bool value);
    void OutString(std::size_t index, const std::string& value);
    void OutVector2(std::size_t index, Vector2Value value);
    void OutVector3(std::size_t index, Vector3Value value);
    void OutVector4(std::size_t index, Vector4Value value);
    void OutQuaternion(std::size_t index, QuaternionValue value);
    void OutObject(std::size_t index, ObjectHandle value);
    void OutNull(std::size_t index);
    void OutValue(std::size_t index, const Scalar& value);
    // 현재 호출의 반환형으로 JSON을 검사한다. 실패하면 호스트 호출의 오류를 함께 설정한다.
    bool ReturnJson(const std::string& text, bool strictIntegers = true);
    void Fail(const std::string& message);
    // 중첩 호출의 실패는 바깥 호스트 호출에도 전달한다. 이후 객체 포인터는 다시 조회한다.
    Result CallScript(const std::string& name, const std::vector<Scalar>& arguments = {});
    Result CallScript(const Scalar& function, const std::vector<Scalar>& arguments = {});

private:
    friend struct HostRuntimeAccess;
    struct Impl;
    explicit HostCall(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}
