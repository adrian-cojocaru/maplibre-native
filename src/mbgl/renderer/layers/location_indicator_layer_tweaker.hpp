#pragma once

#include <mbgl/renderer/layer_tweaker.hpp>
#include <mbgl/gfx/drawable_tweaker.hpp>
#include <mbgl/gfx/uniform_buffer.hpp>
#include <mbgl/style/layers/location_indicator_layer_properties.hpp>

#include <string>

namespace mbgl {

/**
    Location indicator layer specific tweaker
 */
class LocationIndicatorLayerTweaker : public LayerTweaker, gfx::DrawableTweaker {
public:
    LocationIndicatorLayerTweaker(std::string id_,
                                  Immutable<style::LayerProperties> properties,
                                  const mbgl::mat4& projectionCircle_,
                                  const mbgl::mat4& projectionPuck_)
        : LayerTweaker(std::move(id_), properties),
          projectionCircle(projectionCircle_),
          projectionPuck(projectionPuck_) {}

public:
    ~LocationIndicatorLayerTweaker() override = default;

    // layer tweaker
    void execute(LayerGroupBase&, const PaintParameters& params) override;

    // drawable tweaker
    void execute(gfx::Drawable&, PaintParameters&) override;
    void init(gfx::Drawable&) override;

private:
    void tweak(gfx::Drawable&,
               const style::LocationIndicatorPaintProperties::PossiblyEvaluated&,
               const PaintParameters&);

private:
    const mbgl::mat4& projectionCircle;
    const mbgl::mat4& projectionPuck;

    static constexpr bool USE_DRAWABLE_TWEAKER = true;
    style::LocationIndicatorPaintProperties::PossiblyEvaluated _props;
};

} // namespace mbgl
