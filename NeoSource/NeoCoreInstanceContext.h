#pragma once

namespace NeoCore
{
class Instance;

// 백엔드 인스턴스 데이터는 내부 연결만 보관한다. 공개 사용자 데이터와 섞지 않는다.
struct BackendInstanceContext
{
    Instance* owner = nullptr;
    void* host = nullptr;
};
}
