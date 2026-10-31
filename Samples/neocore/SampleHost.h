#pragma once
#include "NeoCoreHost.h"
#include <memory>
#include <string>

class SampleHost
{
public:
    SampleHost();
    NeoCore::HostBindings& Bindings() { return *bindings_; }
    bool Verify(const std::string& sample, NeoCore::Program& program, NeoCore::Error& error);
private:
    std::unique_ptr<NeoCore::HostBindings> bindings_;
    NeoCore::Vector3Value position_;
};

std::shared_ptr<NeoCore::IHttpTransport> CreateSampleHttpTransport();
