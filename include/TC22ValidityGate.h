#pragma once
#include "Tc22Protocol.h"

class Tc22ValidityGate{
public:
    struct Config {
        float blindAreaM = 3.0f;
        float maxRangeM = 1000.0f;
    };
    Tc22ValidityGate() = default;
    explicit Tc22ValidityGate(const Config& cfg)
        : cfg_(cfg) {}

    void configure(const Config& cfg) { cfg_ = cfg; }
    const Config& config() const { return cfg_; }

    Tc22Measurement evaluate(const Tc22Frame& f);

private:
    Config cfg_;
};