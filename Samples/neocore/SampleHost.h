#pragma once
#include "NeoCoreHost.h"
#include <memory>
#include <string>

class SampleHost
{
public:
    explicit SampleHost(const std::string& sample);
    const NeoCore::HostBindings* Bindings() const { return bindings_.get(); }
    bool Verify(const std::string& sample, NeoCore::Program& program, NeoCore::Error& error);
private:
    std::unique_ptr<NeoCore::HostBindings> bindings_;
    NeoCore::Vector3Value position_;
};

std::shared_ptr<NeoCore::IHttpTransport> CreateSampleHttpTransport();
