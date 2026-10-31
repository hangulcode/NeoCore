#include "SampleHost.h"
#include <chrono>
#include <thread>
#include <stdexcept>

namespace
{
class SampleHttpTransport : public NeoCore::IHttpTransport
{
public:
    NeoCore::HttpResponse Send(const NeoCore::HttpRequest& request) override
    {
        NeoCore::HttpResponse response;
        response.success = !request.IsCancelled() && request.url == "https://sample.invalid/echo";
        if (response.success)
        {
            response.body = request.method == "POST" ? request.body : "typed http ok";
            if (request.method == "POST")
            {
                bool header = false;
                for (const auto& item : request.headers)
                    if (item.first == "Content-Type" && item.second == "text/plain") header = true;
                response.success = header;
            }
        }
        if (!response.success) response.error = "unexpected or cancelled sample request";
        return response;
    }
};
NeoCore::Result Finish(NeoCore::Program& program, NeoCore::Result result, std::uint32_t budget = 0)
{
    for (unsigned poll = 0; result.ok() && result.suspended() && poll < 10000; ++poll)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        result = program.Resume(0.0, budget);
    }
    return result;
}
}

std::shared_ptr<NeoCore::IHttpTransport> CreateSampleHttpTransport()
{
    return std::shared_ptr<NeoCore::IHttpTransport>(new SampleHttpTransport());
}

SampleHost::SampleHost(const std::string& sample) : position_(1, 2, 3)
{
    const bool callback = sample == "callback";
    if (!callback && sample != "map_callback") return;

    // 선택한 샘플의 바인딩만 등록하고, 나머지 샘플은 호스트 스키마 없이 컴파일한다.
    NeoCore::HostSchema schema;
    const NeoCore::TypeId object = schema.AddObject(callback ? "SampleServices" : "SampleData");
    const char* global = callback ? "Host" : "g_sData";
    schema.AddGlobal(global, object);
    NeoCore::HostBindingId mul = NeoCore::InvalidHostBinding;
    if (callback)
    {
        const NeoCore::TypeId integer = schema.Types().Int();
        mul = schema.AddMethod(object, "mul", integer, {integer, integer});
    }
    else schema.AddProperty(object, "Transform", schema.Types().Vector3());
    bindings_.reset(new NeoCore::HostBindings(schema,
        [](void* owner, NeoCore::TypeId, NeoCore::ObjectHandle object) -> void* {
            return object.value == 1 ? owner : nullptr;
        }, this));
    bindings_->BindGlobal(global, NeoCore::ObjectHandle(1));
    if (callback)
    {
        bindings_->Bind(mul, [](NeoCore::HostCall& call) {
            call.ReturnInt(call.Int(0) * call.Int(1));
            return true;
        });
    }
    else
    {
        const NeoCore::HostProperty* property = schema.FindProperty(object, "Transform");
        bindings_->Bind(property->getter, [this](NeoCore::HostCall& call) {
            call.ReturnVector3(position_); return true;
        });
        bindings_->Bind(property->setter, [this](NeoCore::HostCall& call) {
            position_ = call.Vector3(0); return true;
        });
    }
}

bool SampleHost::Verify(const std::string& sample, NeoCore::Program& program, NeoCore::Error& error)
{
    NeoCore::Result result;
    bool matched = true;
    if (sample == "callback")
    {
        result = program.Call("Sum", {NeoCore::Scalar::Int(100), NeoCore::Scalar::Int(200)});
        matched = result.ok() && result.value.kind() == NeoCore::Scalar::Kind::Int && result.value.asInt() == 20300;
    }
    else if (sample == "map_callback")
    {
        result = program.Call("Update");
        matched = result.ok() && position_.x == 3 && position_.y == 2 && position_.z == 3;
    }
    else if (sample == "slice_run" || sample == "time_limit")
    {
        NeoCore::ExecutionLimits limits;
        limits.instructionBudget = 100;
        limits.timeoutMs = 100;
        result = program.Call(sample == "slice_run" ? "Slice" : "TimeTest", {}, limits);
        const bool suspended = result.ok() && result.suspended();
        result = Finish(program, result, limits.instructionBudget);
        const std::int64_t expected = sample == "slice_run" ? 499500 : 10;
        matched = suspended && result.ok() && !result.suspended() &&
            result.value.kind() == NeoCore::Scalar::Kind::Int && result.value.asInt() == expected;
    }
    else if (sample == "http")
    {
        result = program.Call("Verify");
        matched = result.ok() && result.value.kind() == NeoCore::Scalar::Kind::Bool && result.value.asBool();
    }
    else if (sample == "module" || sample == "particles" || sample == "math" || sample == "json")
    {
        result = program.Call("CheckPassed");
        matched = result.ok() && !result.suspended() &&
            result.value.kind() == NeoCore::Scalar::Kind::Bool && result.value.asBool();
        if (matched && sample == "module")
        {
            // 동적 문자열도 별도 정적 컴파일과 명시된 export 서명으로 실행한다.
            auto chunk = NeoCore::Program::Compile("export string Chunk() { return \"ok\"; }", error, "chunk.nc");
            if (!chunk) return false;
            for (int i = 0; matched && i < 2; ++i)
            {
                result = chunk->Call("Chunk");
                matched = result.ok() && !result.suspended() &&
                    result.value.kind() == NeoCore::Scalar::Kind::String && result.value.asString() == "ok";
            }
        }
    }
    error = result.error;
    if (!matched && error.ok()) error.message = "sample host result mismatch: " + sample;
    return matched && result.ok() && !result.suspended();
}
