#include <mbgl/renderer/buckets/line_bucket.hpp>
#include <mbgl/renderer/bucket_parameters.hpp>
#include <mbgl/style/layers/line_layer_impl.hpp>
#include <mbgl/util/math.hpp>
#include <mbgl/util/constants.hpp>
#include <mbgl/gfx/polyline_generator.hpp>

#include <cassert>
#include <utility>

namespace mbgl {

using namespace style;

LineBucket::LineBucket(LineBucket::PossiblyEvaluatedLayoutProperties layout_,
                       const std::map<std::string, Immutable<LayerProperties>>& layerPaintProperties,
                       const float zoom_,
                       const uint32_t overscaling_)
    : layout(std::move(layout_)),
      zoom(zoom_),
      overscaling(overscaling_) {
    for (const auto& pair : layerPaintProperties) {
        paintPropertyBinders.emplace(std::piecewise_construct,
                                     std::forward_as_tuple(pair.first),
                                     std::forward_as_tuple(getEvaluated<LineLayerProperties>(pair.second), zoom));
    }
}

LineBucket::~LineBucket() {
    sharedVertices->release();
}

static float cross(const Point<int16_t>& a, const Point<int16_t>& b) {
    return a.x * b.y - a.y * b.x;
}

// Intersect two lines
static std::optional<Point<int16_t>> intersectLines(Point<int16_t> a0,
                                             Point<int16_t> a1,
                                             Point<int16_t> b0,
                                             Point<int16_t> b1) {
    const Point<int16_t> r = a1 - a0;
    const Point<int16_t> s = b1 - b0;

    const float rxs = cross(r, s);

    // parallel
    if (rxs == 0.0f) {
        return std::nullopt;
    }

    const float q_pxr = cross(b0 - a0, r);

    // colinear
    if (q_pxr == 0.0f) {
        return std::nullopt;
    }

    float t = cross(b0 - a0, s) / rxs;
    float u = cross(b0 - a0, r) / rxs;

    // Check if intersection is within both segments
    if (t >= 0.0f && t <= 1.0f && u >= 0.0f && u <= 1.0f) {
        return a0 + Point<int16_t>(r.x * t, r.y * t);
    }

    // intersection outside segments
    return std::nullopt;
}

struct IntersectionPoint {
    Point<int16_t> intersection;
    std::array<Point<double>, 4> bisectors;
    Point<int16_t> a0, a1;
    Point<int16_t> b0, b1;
};

static Point<double> bisector(const Point<int16_t>& a, const Point<int16_t>& b, const Point<int16_t>& c) {
    return util::unit(convertPoint<double>(a - b)) + util::unit(convertPoint<double>(c - b));
}

static std::vector<IntersectionPoint> intersectGeometry(const GeometryCollection& geometry0,
                                                        const GeometryCollection& geometry1) {
    std::vector<IntersectionPoint> points;

    for (auto& line0 : geometry0) {
        for (auto& line1 : geometry1) {

            for (size_t index0 = 1; index0 < line0.size(); ++index0) {
                for (size_t index1 = 1; index1 < line1.size(); ++index1) {
                    const auto& a0 = line0[index0 - 1];
                    const auto& a1 = line0[index0];

                    const auto& b0 = line1[index1 - 1];
                    const auto& b1 = line1[index1];

                    const auto point = intersectLines(a0, a1, b0, b1);
                    if (!point) {
                        continue;
                    }

                    points.push_back({.intersection = point.value(),
                                      .bisectors =
                                          {
                                              bisector(a0, point.value(), b0),
                                              bisector(a0, point.value(), b1),
                                              bisector(a1, point.value(), b0),
                                              bisector(a1, point.value(), b1),
                                          },
                                      .a0 = a0,
                                      .a1 = a1,
                                      .b0 = b0,
                                      .b1 = b1});
                }
            }

        }
    }

    return points;
}

void LineBucket::addFeature(const GeometryTileFeature& feature,
                            const GeometryCollection& geometryCollection,
                            const ImagePositions& patternPositions,
                            const PatternLayerMap& patternDependencies,
                            std::size_t index,
                            const CanonicalTileID& canonical) {

#if 1
    // TODO check for what features/layers is this enabled
    // TODO make scaled configurable + dynamic based on the line segment?
    constexpr float scale = 1200.0f;

    if (feature.getType() == FeatureType::LineString) {
        const auto intersectionPoints = intersectGeometry(intersections.featureGeometry, geometryCollection);

        for (const auto& intersectionPoint : intersectionPoints) {
            // TODO move this to separate shader
            #if 0
            for (const auto& bis : intersectionPoint.bisectors) {
                const GeometryCoordinates line = {
                    {intersectionPoint.intersection + Point<int16_t>(bis.x * scale, bis.y * scale)},
                    intersectionPoint.intersection};

                addGeometry(line, feature, canonical);
            }
            #else
            const auto vertex = [&](const Point<int16_t>& v) {
                return layoutVertex(v, {}, false, false, static_cast<int8_t>(0), static_cast<int32_t>(0));
            };

            unsigned int indexCount = 0;
            const auto triangle = [&](const Point<int16_t>& a, const Point<int16_t>& b, const Point<int16_t>& c) {
                intersections.vertices.emplace_back(vertex(a));
                intersections.vertices.emplace_back(vertex(b));
                intersections.vertices.emplace_back(vertex(c));

                intersections.triangles.emplace_back(indexCount, indexCount + 1, indexCount + 2);
                indexCount += 3;
            };

            triangle(intersectionPoint.a0, intersectionPoint.intersection, intersectionPoint.b0);
            triangle(intersectionPoint.a0, intersectionPoint.intersection, intersectionPoint.b1);
            triangle(intersectionPoint.a1, intersectionPoint.intersection, intersectionPoint.b0);
            triangle(intersectionPoint.a1, intersectionPoint.intersection, intersectionPoint.b1);

            intersections.segments.push_back(SegmentBase(0, 0, intersections.vertices.elements(), intersections.triangles.elements()));
            
            #endif
        }

        for (auto& line : geometryCollection) {
            intersections.featureGeometry.push_back(line);
        }
    } else {
        // TODO support others (polygon, point?) or limit?
        Log::Warning(Event::General, "Unsupported road geometry: " + std::to_string(static_cast<int>(feature.getType())));
    }
#endif

    for (auto& line : geometryCollection) {
        addGeometry(line, feature, canonical);
    }

    for (auto& pair : paintPropertyBinders) {
        const auto it = patternDependencies.find(pair.first);
        if (it != patternDependencies.end()) {
            pair.second.populateVertexVectors(
                feature, vertices.elements(), index, patternPositions, it->second, canonical);
        } else {
            pair.second.populateVertexVectors(feature, vertices.elements(), index, patternPositions, {}, canonical);
        }
    }
}

void LineBucket::addGeometry(const GeometryCoordinates& coordinates,
                             const GeometryTileFeature& feature,
                             const CanonicalTileID& canonical) {
    // Ignore empty coordinates.
    if (coordinates.empty()) {
        return;
    }
    gfx::PolylineGenerator<LineLayoutVertex, SegmentBase> generator(
        vertices,
        LineBucket::layoutVertex,
        segments,
        [](std::size_t vertexOffset, std::size_t indexOffset) -> SegmentBase {
            return SegmentBase(vertexOffset, indexOffset);
        },
        [](auto& seg) -> SegmentBase& { return seg; },
        triangles);

    gfx::PolylineGeneratorOptions options;

    options.type = feature.getType();
    const std::size_t len = [&coordinates] {
        std::size_t l = coordinates.size();
        // If the line has duplicate vertices at the end, adjust length to remove them.
        while (l >= 2 && coordinates[l - 1] == coordinates[l - 2]) {
            l--;
        }
        return l;
    }();

    const std::size_t first = [&coordinates, &len] {
        std::size_t i = 0;
        // If the line has duplicate vertices at the start, adjust index to remove them.
        while (i + 1 < len && coordinates[i] == coordinates[i + 1]) {
            i++;
        }
        return i;
    }();

    // Ignore invalid geometry.
    const std::size_t minLen = (options.type == FeatureType::Polygon ? 3 : 2);
    if (len < minLen) {
        // Warn once, but only if the source geometry is invalid, not if de-duplication made it invalid.
        // This happens, e.g., when attempting to use a GeoJSON `MultiPoint`
        // or `MLNPointCollectionFeature` as the source for a line layer.
        // Unfortunately, we cannot show the layer or source name from here.
        if (coordinates.size() < minLen) {
            static bool warned = false; // not thread-safe, there's a small chance of warning more than once
            if (!warned) {
                warned = true;
                Log::Warning(Event::General, "Invalid geometry in line layer");
            }
        }
        return;
    }

    const auto clip_start = feature.getValue("mapbox_clip_start");
    const auto clip_end = feature.getValue("mapbox_clip_end");
    if (clip_start && clip_end) {
        double total_length = 0.0;
        for (std::size_t i = first; i < len - 1; ++i) {
            total_length += util::dist<double>(coordinates[i], coordinates[i + 1]);
        }

        options.clipDistances = gfx::PolylineGeneratorDistances{
            *numericValue<double>(*clip_start), *numericValue<double>(*clip_end), total_length};
    }

    options.joinType = layout.evaluate<LineJoin>(zoom, feature, canonical);
    options.miterLimit = options.joinType == LineJoinType::Bevel ? 1.05f
                                                                 : static_cast<float>(layout.get<LineMiterLimit>());
    options.beginCap = layout.get<LineCap>();
    options.endCap = options.type == FeatureType::Polygon ? LineCapType::Butt : LineCapType(layout.get<LineCap>());
    options.roundLimit = layout.get<LineRoundLimit>();
    options.overscaling = overscaling;

    generator.generate(coordinates, options);
}

void LineBucket::upload([[maybe_unused]] gfx::UploadPass& uploadPass) {
    uploaded = true;

    // discard all cached features on first render
    intersections.featureGeometry.clear();
}

bool LineBucket::hasData() const {
    return !segments.empty();
}

namespace {
template <class Property>
float get(const LinePaintProperties::PossiblyEvaluated& evaluated,
          const std::string& id,
          const std::map<std::string, LineBinders>& paintPropertyBinders) {
    auto it = paintPropertyBinders.find(id);
    if (it == paintPropertyBinders.end() || !it->second.statistics<Property>().max()) {
        return evaluated.get<Property>().constantOr(Property::defaultValue());
    } else {
        return *it->second.statistics<Property>().max();
    }
}
} // namespace

float LineBucket::getQueryRadius(const RenderLayer& layer) const {
    const auto& evaluated = getEvaluated<LineLayerProperties>(layer.evaluatedProperties);
    const std::array<float, 2>& translate = evaluated.get<LineTranslate>();
    float offset = get<LineOffset>(evaluated, layer.getID(), paintPropertyBinders);
    float lineWidth = get<LineWidth>(evaluated, layer.getID(), paintPropertyBinders);
    float gapWidth = get<LineGapWidth>(evaluated, layer.getID(), paintPropertyBinders);
    if (gapWidth) {
        lineWidth = gapWidth + 2 * lineWidth;
    }

    return lineWidth / 2.0f + std::abs(offset) + util::length(translate[0], translate[1]);
}

void LineBucket::update(const FeatureStates& states,
                        const GeometryTileLayer& layer,
                        const std::string& layerID,
                        const ImagePositions& imagePositions) {
    auto it = paintPropertyBinders.find(layerID);
    if (it != paintPropertyBinders.end()) {
        it->second.updateVertexVectors(states, layer, imagePositions);
        uploaded = false;

        sharedVertices->updateModified();
    }
}

} // namespace mbgl
